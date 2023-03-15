#include <stddef.h>
#include <Xinput.h>
#include <SimpleIni.h>

using namespace RE;
using namespace RE::BSScript;
using namespace SKSE;
using namespace SKSE::log;
using namespace SKSE::stl;

const MessagingInterface* g_message = nullptr;
const TaskInterface* g_task = nullptr;
PlayerCharacter* p;
PlayerControls* pc;
BGSAction* actionRightPowerAttack;
BGSAction* actionRightAttack;
BGSAction* actionDualPowerAttack;
UI* mm;
ControlMap* im;
UserEvents* inputString;
CSimpleIniA ini(true, false, false);
uint32_t attackKey[INPUT_DEVICE::kTotal] = { 0xFF, 0xFF, 0xFF };
uint32_t blockKey[INPUT_DEVICE::kTotal] = { 0xFF, 0xFF, 0xFF };
int paKey = 257;
int modifierKey = -1;
bool keyComboPressed = false;
bool onlyFirstAttack = false;
bool onlyDuringAttack = false;

int dualPaKey = 257;
int dualModifierKey = -1;
bool dualKeyComboPressed = false;
bool dualOnlyFirstAttack = false;
bool dualOnlyDuringAttack = false;

bool allowZeroStamina = false;
int longPressMode = 2;

bool isAttacking = false;
bool attackWindow = false;
bool isLongPressPatched = false;
float fInitialPowerAttackDelay = 0.3f;
float fInitialPowerBashDelay = 0.3f;

bool IsRidingHorse(Actor* a) {
	return (a->AsActorState()->actorState1.sitSleepState == SIT_SLEEP_STATE::kRidingMount);
}

bool IsInKillmove(Actor* a) {
	return a->GetActorRuntimeData().boolFlags.all(Actor::BOOL_FLAGS::kIsInKillMove);
}

//Enums from SKSE to get DXScanCodes
enum {
	// first 256 for keyboard, then 8 mouse buttons, then mouse wheel up, wheel down, then 16 gamepad buttons
	kMacro_KeyboardOffset = 0,		// not actually used, just for self-documentation
	kMacro_NumKeyboardKeys = 256,

	kMacro_MouseButtonOffset = kMacro_NumKeyboardKeys,	// 256
	kMacro_NumMouseButtons = 8,

	kMacro_MouseWheelOffset = kMacro_MouseButtonOffset + kMacro_NumMouseButtons,	// 264
	kMacro_MouseWheelDirections = 2,

	kMacro_GamepadOffset = kMacro_MouseWheelOffset + kMacro_MouseWheelDirections,	// 266
	kMacro_NumGamepadButtons = 16,

	kMaxMacros = kMacro_GamepadOffset + kMacro_NumGamepadButtons	// 282
};

enum {
	kGamepadButtonOffset_DPAD_UP = kMacro_GamepadOffset,	// 266
	kGamepadButtonOffset_DPAD_DOWN,
	kGamepadButtonOffset_DPAD_LEFT,
	kGamepadButtonOffset_DPAD_RIGHT,
	kGamepadButtonOffset_START,
	kGamepadButtonOffset_BACK,
	kGamepadButtonOffset_LEFT_THUMB,
	kGamepadButtonOffset_RIGHT_THUMB,
	kGamepadButtonOffset_LEFT_SHOULDER,
	kGamepadButtonOffset_RIGHT_SHOULDER,
	kGamepadButtonOffset_A,
	kGamepadButtonOffset_B,
	kGamepadButtonOffset_X,
	kGamepadButtonOffset_Y,
	kGamepadButtonOffset_LT,
	kGamepadButtonOffset_RT	// 281
};

uint32_t GamepadMaskToKeycode(uint32_t keyMask) {
	switch (keyMask) {
		case XINPUT_GAMEPAD_DPAD_UP:		return kGamepadButtonOffset_DPAD_UP;
		case XINPUT_GAMEPAD_DPAD_DOWN:		return kGamepadButtonOffset_DPAD_DOWN;
		case XINPUT_GAMEPAD_DPAD_LEFT:		return kGamepadButtonOffset_DPAD_LEFT;
		case XINPUT_GAMEPAD_DPAD_RIGHT:		return kGamepadButtonOffset_DPAD_RIGHT;
		case XINPUT_GAMEPAD_START:			return kGamepadButtonOffset_START;
		case XINPUT_GAMEPAD_BACK:			return kGamepadButtonOffset_BACK;
		case XINPUT_GAMEPAD_LEFT_THUMB:		return kGamepadButtonOffset_LEFT_THUMB;
		case XINPUT_GAMEPAD_RIGHT_THUMB:	return kGamepadButtonOffset_RIGHT_THUMB;
		case XINPUT_GAMEPAD_LEFT_SHOULDER:	return kGamepadButtonOffset_LEFT_SHOULDER;
		case XINPUT_GAMEPAD_RIGHT_SHOULDER: return kGamepadButtonOffset_RIGHT_SHOULDER;
		case XINPUT_GAMEPAD_A:				return kGamepadButtonOffset_A;
		case XINPUT_GAMEPAD_B:				return kGamepadButtonOffset_B;
		case XINPUT_GAMEPAD_X:				return kGamepadButtonOffset_X;
		case XINPUT_GAMEPAD_Y:				return kGamepadButtonOffset_Y;
		case 0x9:							return kGamepadButtonOffset_LT;
		case 0xA:							return kGamepadButtonOffset_RT;
		default:							return kMaxMacros; // Invalid
	}
}

//Store key settings after load/journal menu close for optimization
//Since controlMap does not have a lock, it can lead to crashes if it is called every frame.
void GetKeySettings() {
	for (int i = INPUT_DEVICE::kKeyboard; i <= INPUT_DEVICE::kGamepad; ++i) {
		switch (i) {
			case INPUT_DEVICE::kKeyboard:
				attackKey[i] = im->GetMappedKey(inputString->rightAttack, INPUT_DEVICE::kKeyboard);
				blockKey[i] = im->GetMappedKey(inputString->leftAttack, INPUT_DEVICE::kKeyboard);
				break;
			case INPUT_DEVICE::kMouse:
				attackKey[i] = kMacro_NumKeyboardKeys + im->GetMappedKey(inputString->rightAttack, INPUT_DEVICE::kMouse);
				blockKey[i] = kMacro_NumKeyboardKeys + im->GetMappedKey(inputString->leftAttack, INPUT_DEVICE::kMouse);
				break;
			case INPUT_DEVICE::kGamepad:
				attackKey[i] = GamepadMaskToKeycode(im->GetMappedKey(inputString->rightAttack, INPUT_DEVICE::kGamepad));
				blockKey[i] = GamepadMaskToKeycode(im->GetMappedKey(inputString->leftAttack, INPUT_DEVICE::kGamepad));
				break;
		}
	}
}

void PerformAction(BGSAction* action, Actor* a) {
	g_task->AddTask([action, a]() {
		std::unique_ptr<TESActionData> data(TESActionData::Create());
		data->source = NiPointer<TESObjectREFR>(a);
		data->action = action;
		typedef bool func_t(TESActionData*);
		REL::Relocation<func_t> func{ RELOCATION_ID(40551, 41557) };
		func(data.get());
	});
}

//For Jumping Attack/Vanguard
void AltPowerAttack() {
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	PerformAction(actionRightPowerAttack, p);
	//SendConsoleCommand(powerAttack);
}

void PowerAttack() {
	bool isJumping = false;
	p->GetGraphVariableBool("bInJumpState", isJumping);
	if (isJumping || p->AsActorState()->actorState2.wantBlocking) {
		//SendConsoleCommand(lightAttack);
		PerformAction(actionRightAttack, p);
		std::thread thread(AltPowerAttack);
		thread.detach();
	}
	else PerformAction(actionRightPowerAttack, p);//SendConsoleCommand(powerAttack);
}

void DualPowerAttack() {
	PerformAction(actionDualPowerAttack, p);//SendConsoleCommand(powerAttack);
}

void RepeatAttack() {
	PerformAction(actionRightAttack, p);
	//SendConsoleCommand(lightAttack);
}

//Load configs from the ini file
void LoadConfigs() {
	logger::info("Loading configs");
	ini.LoadFile("Data\\SKSE\\Plugins\\OneClickPowerAttack.ini");
	paKey = std::stoi(ini.GetValue("General", "Keycode", "257"));
	modifierKey = std::stoi(ini.GetValue("General", "ModifierKey", "-1"));
	longPressMode = std::stoi(ini.GetValue("General", "LongPressMode", "0"));
	allowZeroStamina = std::stoi(ini.GetValue("General", "AllowZeroStamina", "0")) > 0;
	onlyFirstAttack = std::stoi(ini.GetValue("General", "SkipModifierDuringCombo", "0")) > 0;
	onlyDuringAttack = std::stoi(ini.GetValue("General", "OnlyDuringAttack", "0")) > 0;
	bool disableBlockKey = std::stoi(ini.GetValue("General", "DisableBlockKey", "0")) > 0;
	dualPaKey = std::stoi(ini.GetValue("DualAttack", "Keycode", "257"));
	dualModifierKey = std::stoi(ini.GetValue("DualAttack", "ModifierKey", "42"));
	dualOnlyFirstAttack = std::stoi(ini.GetValue("DualAttack", "SkipModifierDuringCombo", "0")) > 0;
	dualOnlyDuringAttack = std::stoi(ini.GetValue("DualAttack", "OnlyDuringAttack", "0")) > 0;
	ini.Reset();
	logger::info("Done");

	//This isn't the best way to do it but it's the easiest way to support all SE/AE builds
	if (longPressMode > 0 && !isLongPressPatched) {
		for (auto it = INISettingCollection::GetSingleton()->settings.begin(); it != INISettingCollection::GetSingleton()->settings.end(); ++it) {
			if (strcmp((*it)->name, "fInitialPowerAttackDelay:Controls") == 0) {
				fInitialPowerAttackDelay = (*it)->GetFloat();
				(*it)->data.f = 999999.f;
				pc->attackBlockHandler->initialPowerAttackDelay = 999999.f;
			}
			else if (strcmp((*it)->name, "fInitialPowerBashDelay:Controls") == 0) {
				fInitialPowerBashDelay = (*it)->GetFloat();
				(*it)->data.f = 999999.f;
			}
		}
		isLongPressPatched = true;
	}
	else if (isLongPressPatched) {
		for (auto it = INISettingCollection::GetSingleton()->settings.begin(); it != INISettingCollection::GetSingleton()->settings.end(); ++it) {
			if (strcmp((*it)->name, "fInitialPowerAttackDelay:Controls") == 0) {
				(*it)->data.f = fInitialPowerAttackDelay;
				pc->attackBlockHandler->initialPowerAttackDelay = fInitialPowerAttackDelay;
			}
			else if (strcmp((*it)->name, "fInitialPowerBashDelay:Controls") == 0) {
				(*it)->data.f = fInitialPowerBashDelay;
			}
		}
		isLongPressPatched = false;
	}

	//Unbind block key from all devices
	if (disableBlockKey) {
		for (int device = 0; device < 3; ++device) {
			for (auto it = im->controlMap[UserEvents::INPUT_CONTEXT_ID::kGameplay]->deviceMappings[device].begin(); it != im->controlMap[UserEvents::INPUT_CONTEXT_ID::kGameplay]->deviceMappings[device].end(); ++it) {
				if ((*it).eventID == inputString->leftAttack) {
					(*it).inputKey = 0xFF;
				}
			}
		}
		UserEventEnabled evn;
		evn.newUserEventFlag = im->enabledControls;
		evn.oldUserEventFlag = im->enabledControls;
		im->SendEvent(&evn);
	}
	GetKeySettings();
}

//Fired when the user presses the attack or block key
class HookAttackBlockHandler {
public:
	typedef void (HookAttackBlockHandler::* FnProcessButton) (ButtonEvent*, void*);
	void ProcessButton(ButtonEvent* a_event, void* a_data) {
		TESObjectWEAP* weap = reinterpret_cast<TESObjectWEAP*>(p->GetEquippedObject(false));
		if (!IsRidingHorse(p) && !IsInKillmove(p) && (!weap || !weap->IsBow())) {
			uint32_t keyMask = a_event->idCode;
			uint32_t keyCode;
			// Mouse
			if (a_event->device.get() == INPUT_DEVICE::kMouse) {
				keyCode = kMacro_NumKeyboardKeys + keyMask;
			}
			// Gamepad
			else if (a_event->device.get() == INPUT_DEVICE::kGamepad) {
				keyCode = GamepadMaskToKeycode(keyMask);
			}
			// Keyboard
			else
				keyCode = keyMask;

			float timer = a_event->heldDownSecs;
			bool isDown = a_event->value != 0 && timer == 0.0;
			bool isHeld = a_event->value != 0 && timer > 0.5;
			bool isUp = a_event->value == 0 && timer != 0;
			//Check if it's attack/block
			bool isAttackKey = keyCode == attackKey[a_event->device.get()];
			bool isBlockKey = keyCode == blockKey[a_event->device.get()];

			//Send release event
			if (isAttackKey && isUp) {
				p->NotifyAnimationGraph("attackRelease");
			}

			if (longPressMode > 0) {
				//Repeat attack logic & block long press
				if (isAttackKey) {
					if (isHeld) {
						if (longPressMode == 2 && attackWindow) {
							RepeatAttack();
						}
						return;
					}
				};

				//To provide consistency, don't block even if there's no PA combo connected to the attack
				if (onlyDuringAttack && isBlockKey && isAttacking)
					return;
			}
		}

		FnProcessButton fn = fnHash.at(*(uintptr_t*)this);
		if (fn)
			(this->*fn)(a_event, a_data);
	}

	static void Hook() {
		REL::Relocation<uintptr_t> vtable{ VTABLE_AttackBlockHandler[0] };
		FnProcessButton fn = stl::unrestricted_cast<FnProcessButton>(vtable.write_vfunc(4, &HookAttackBlockHandler::ProcessButton));
		fnHash.insert(std::pair<uintptr_t, FnProcessButton>(vtable.address(), fn));
	}
private:
	static std::unordered_map<uintptr_t, FnProcessButton> fnHash;
};
std::unordered_map<uintptr_t, HookAttackBlockHandler::FnProcessButton> HookAttackBlockHandler::fnHash;

//Fired on anim events
class HookAnimGraphEvent {
public:
	typedef BSEventNotifyControl (HookAnimGraphEvent::* FnReceiveEvent)(BSAnimationGraphEvent* evn, BSTEventSource<BSAnimationGraphEvent>* dispatcher);

	BSEventNotifyControl ReceiveEventHook(BSAnimationGraphEvent* evn, BSTEventSource<BSAnimationGraphEvent>* src) {
		Actor* a = stl::unrestricted_cast<Actor*>(evn->holder);
		if (a) {
			//Record actor states
			if (!IsRidingHorse(a) && !IsInKillmove(a)) {
				ATTACK_STATE_ENUM currentState = (a->AsActorState()->actorState1.meleeAttackState);
				if (currentState >= ATTACK_STATE_ENUM::kSwing && currentState <= ATTACK_STATE_ENUM::kBash) {
					isAttacking = true;
				}
				else {
					isAttacking = false;
				}
			}
			else {
				isAttacking = false;
			}
			if (evn->tag == "MCO_WinOpen") attackWindow = true;
			if (evn->tag == "MCO_WinClose") attackWindow = false;
		}
		FnReceiveEvent fn = fnHash.at(*(uintptr_t*)this);
		return fn ? (this->*fn)(evn, src) : BSEventNotifyControl::kContinue;
	}

	static void Hook() {
		REL::Relocation<uintptr_t> vtable{ VTABLE_PlayerCharacter[2] };
		FnReceiveEvent fn = stl::unrestricted_cast<FnReceiveEvent>(vtable.write_vfunc(1, &HookAnimGraphEvent::ReceiveEventHook));
		fnHash.insert(std::pair<uintptr_t, FnReceiveEvent>(vtable.address(), fn));
	}
private:
	static std::unordered_map<uintptr_t, FnReceiveEvent> fnHash;
};
std::unordered_map<uintptr_t, HookAnimGraphEvent::FnReceiveEvent> HookAnimGraphEvent::fnHash;

//Fired on input events (obviously)
using InputEvents = InputEvent*;
class InputEventHandler : public BSTEventSink<InputEvents> {
public:
	virtual BSEventNotifyControl ProcessEvent(const InputEvents* evns, BSTEventSource<InputEvents>* dispatcher) override {
		if (!*evns)
			return BSEventNotifyControl::kContinue;

		if (IsRidingHorse(p) || IsInKillmove(p))
			return BSEventNotifyControl::kContinue;

		//Several conditions where OCPA shouldn't be working
		uint32_t controlFlag = (uint32_t)UserEvents::USER_EVENT_FLAG::kMovement & (uint32_t)UserEvents::USER_EVENT_FLAG::kLooking;
		if (mm->numPausesGame > 0 || ((im->enabledControls.underlying() & controlFlag) != controlFlag) || mm->IsMenuOpen("Dialogue Menu"sv)
			|| p->AsActorState()->actorState1.sitSleepState != SIT_SLEEP_STATE::kNormal || (!allowZeroStamina && p->AsActorValueOwner()->GetActorValue(ActorValue::kStamina) <= 0))
			return BSEventNotifyControl::kContinue;

		for (InputEvent* e = *evns; e; e = e->next) {
			switch (e->eventType.get()) {
				case INPUT_EVENT_TYPE::kButton:
					ButtonEvent* a_event = e->AsButtonEvent();

					uint32_t keyMask = a_event->idCode;
					uint32_t keyCode;

					// Mouse
					if (a_event->device.get() == INPUT_DEVICE::kMouse) {
						keyCode = kMacro_NumKeyboardKeys + keyMask;
					}
					// Gamepad
					else if (a_event->device.get() == INPUT_DEVICE::kGamepad) {
						keyCode = GamepadMaskToKeycode(keyMask);
					}
					// Keyboard
					else
						keyCode = keyMask;

					// Valid scancode?
					if (keyCode >= kMaxMacros)
						continue;

					float timer = a_event->heldDownSecs;
					bool isDown = a_event->value != 0 && timer == 0.0;
					bool isHeld = a_event->value != 0 && timer > 0.5;
					bool isUp = a_event->value == 0 && timer != 0;

					if (keyCode == modifierKey && isDown) keyComboPressed = true;
					if (keyCode == modifierKey && isUp) keyComboPressed = false;
					//Simplified logic for power attacks
					//onlyDuringAttack? -> check isAttacking
					//onlyFirstAttack? -> check keyComboPressed if it's not the first attack
					//no modifier key? -> just power attack whenever the key was pressed
					if (keyCode == paKey && isDown) {
						if ((isAttacking && onlyDuringAttack) || !onlyDuringAttack) {
							if (modifierKey >= 0) {
								if (onlyFirstAttack) {
									if ((!isAttacking && keyComboPressed) || isAttacking) {
										PowerAttack();
									}
								}
								else {
									if (keyComboPressed) {
										PowerAttack();
									}
								}
							}
							else {
								PowerAttack();
							}
						}
					}

					//Copypasta for dual attack
					if (keyCode == dualModifierKey && isDown) dualKeyComboPressed = true;
					if (keyCode == dualModifierKey && isUp) dualKeyComboPressed = false;
					if (keyCode == dualPaKey && isDown) {
						if ((isAttacking && dualOnlyDuringAttack) || !dualOnlyDuringAttack) {
							if (dualModifierKey >= 0) {
								if (dualOnlyFirstAttack) {
									if ((!isAttacking && dualKeyComboPressed) || isAttacking) {
										DualPowerAttack();
									}
								}
								else {
									if (dualKeyComboPressed) {
										DualPowerAttack();
									}
								}
							}
							else {
								DualPowerAttack();
							}
						}
					}
					break;
			}
		}
		return BSEventNotifyControl::kContinue;
	}
	TES_HEAP_REDEFINE_NEW();
};

//Fired on menu events
class MenuWatcher : public BSTEventSink<MenuOpenCloseEvent> {
public:
	virtual BSEventNotifyControl ProcessEvent(const MenuOpenCloseEvent* evn, BSTEventSource<MenuOpenCloseEvent>* dispatcher) override {
		//Load configs after loading
		if (evn->menuName == InterfaceStrings::GetSingleton()->loadingMenu && evn->opening) {
			LoadConfigs();
		}
		//Load key settings after closing journal menu
		else if (evn->menuName == InterfaceStrings::GetSingleton()->journalMenu && !evn->opening) {
			GetKeySettings();
		}
		return BSEventNotifyControl::kContinue;
	}
	TES_HEAP_REDEFINE_NEW();
};

namespace {
	void InitializeLogging() {
		auto path = log_directory();
		if (!path) {
			report_and_fail("Unable to lookup SKSE logs directory.");
		}
		*path /= PluginDeclaration::GetSingleton()->GetName();
		*path += L".log";

		std::shared_ptr<spdlog::logger> log;
		if (IsDebuggerPresent()) {
			log = std::make_shared<spdlog::logger>(
				"Global", std::make_shared<spdlog::sinks::msvc_sink_mt>());
		}
		else {
			log = std::make_shared<spdlog::logger>(
				"Global", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
		}
		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%t] [%s:%#] %v");
	}
}

SKSEPluginLoad(const LoadInterface* skse) {
	InitializeLogging();

	auto* plugin = PluginDeclaration::GetSingleton();
	auto version = plugin->GetVersion();
	logger::info("{} {} is loading...", plugin->GetName(), version);


	Init(skse);
	g_task = GetTaskInterface();
	g_message = GetMessagingInterface();
	g_message->RegisterListener([](MessagingInterface::Message* msg) -> void {
		if (msg->type == MessagingInterface::kDataLoaded) {
			BSInputDeviceManager* inputEventDispatcher = BSInputDeviceManager::GetSingleton();
			if (inputEventDispatcher) {
				p = PlayerCharacter::GetSingleton();
				pc = PlayerControls::GetSingleton();
				im = ControlMap::GetSingleton();
				mm = UI::GetSingleton();
				actionRightAttack = (BGSAction*)TESForm::LookupByID(0x13005);
				actionRightPowerAttack = (BGSAction*)TESForm::LookupByID(0x13383);
				actionDualPowerAttack = (BGSAction*)TESForm::LookupByID(0x2E2F7);
				inputString = UserEvents::GetSingleton();
				InputEventHandler* handler = new InputEventHandler();
				inputEventDispatcher->AddEventSink(handler);
				MenuWatcher* mw = new MenuWatcher();
				mm->GetEventSource<MenuOpenCloseEvent>()->AddEventSink(mw);
				HookAnimGraphEvent::Hook();
				HookAttackBlockHandler::Hook();
				logger::info("PlayerCharacter {:#x}", (uintptr_t)p);
			}
			else {
				logger::critical("Failed to register inputEventHandler");
			}
		}
	});

	logger::info("{} has finished loading.", plugin->GetName());
	return true;
}

#include <stddef.h>
#include <Xinput.h>
#include <SimpleIni.h>

using namespace RE;
using namespace RE::BSScript;
using namespace SKSE;
using namespace SKSE::log;
using namespace SKSE::stl;

const MessagingInterface* g_message = nullptr;
PlayerCharacter* p;
PlayerControls* pc;
UI* mm;
ControlMap* im;
IMenu* console;
UserEvents* inputString;
CSimpleIniA ini(true, false, false);
uint32_t attackKeyboard;
uint32_t attackMouse;
uint32_t attackGamepad;
int paKey = 257;
int modifierKey = -1;
bool keyComboPressed = false;
bool onlyFirstAttack = false;
bool allowZeroStamina = false;
int longPressMode = 2;
bool isAttacking = false;
bool isJumping = false;
bool attackWindow = false;
bool isLongPressPatched = false;
float fInitialPowerAttackDelay = 0.3f;
float fInitialPowerBashDelay = 0.3f;
const static std::string powerAttack = "player.pa ActionRightPowerAttack";
const static std::string lightAttack = "player.pa ActionRightAttack";

bool IsRidingHorse(Actor* a) {
	return (a->actorState1.sitSleepState == SIT_SLEEP_STATE::kRidingMount);
}

bool IsInKillmove(Actor* a) {
	return a->boolFlags.all(Actor::BOOL_FLAGS::kIsInKillMove);
}

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

void GetAttackKeys() {
	attackKeyboard = im->GetMappedKey(inputString->rightAttack, INPUT_DEVICE::kKeyboard);
	attackMouse = kMacro_NumKeyboardKeys + im->GetMappedKey(inputString->rightAttack, INPUT_DEVICE::kMouse);
	attackGamepad = GamepadMaskToKeycode(im->GetMappedKey(inputString->rightAttack, INPUT_DEVICE::kGamepad));
}

void SendConsoleCommand(std::string s) {
	std::array<GFxValue, 2> args;
	console->uiMovie->CreateString(&args[0], "ExecuteCommand");
	console->uiMovie->CreateArray(&args[1]);
	GFxValue str;
	console->uiMovie->CreateString(&str, s.c_str());
	args[1].PushBack(str);

	console->uiMovie->Invoke("gfx.io.GameDelegate.call", nullptr, args.data(), args.size());
}

//For Jumping Attack/Vanguard
void AltPowerAttack() {
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	SendConsoleCommand(powerAttack);
}

void PowerAttack() {
	if (isJumping || p->actorState2.wantBlocking) {
		SendConsoleCommand(lightAttack);
		std::thread thread(AltPowerAttack);
		thread.detach();
	}
	else SendConsoleCommand(powerAttack);
}

void RepeatAttack() {
	SendConsoleCommand(lightAttack);
}

void LoadConfigs() {
	logger::info("Loading configs");
	ini.LoadFile("Data\\SKSE\\Plugins\\OneClickPowerAttack.ini");
	paKey = std::stoi(ini.GetValue("General", "Keycode", "257"));
	modifierKey = std::stoi(ini.GetValue("General", "ModifierKey", "-1"));
	longPressMode = std::stoi(ini.GetValue("General", "LongPressMode", "0"));
	allowZeroStamina = std::stoi(ini.GetValue("General", "AllowZeroStamina", "0")) > 0;
	onlyFirstAttack = std::stoi(ini.GetValue("General", "SkipModifierDuringCombo", "0")) > 0;
	bool disableLongPress = std::stoi(ini.GetValue("General", "DisableLongPress", "1")) > 0;
	bool disableBlockKey = std::stoi(ini.GetValue("General", "DisableBlockKey", "0")) > 0;
	ini.Reset();
	logger::info("Keycode {}", paKey);
	logger::info("Done");

	if (disableLongPress && !isLongPressPatched) {
		for (auto it = INISettingCollection::GetSingleton()->settings.begin(); it != INISettingCollection::GetSingleton()->settings.end(); ++it) {
			if (strcmp((*it)->name, "fInitialPowerAttackDelay:Controls") == 0) {
				fInitialPowerAttackDelay = (*it)->GetFloat();
				(*it)->data.f = 999999.f;
				pc->attackBlockHandler->initialPowerAttackDelay = 999999.f;
				logger::info("fInitialPowerAttackDelay set");
			}
			else if (strcmp((*it)->name, "fInitialPowerBashDelay:Controls") == 0) {
				fInitialPowerBashDelay = (*it)->GetFloat();
				(*it)->data.f = 999999.f;
				logger::info("fInitialPowerBashDelay set");
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
	GetAttackKeys();
}

class HookAttackBlockHandler {
public:
	typedef void (HookAttackBlockHandler::* FnProcessButton) (ButtonEvent*, void*);
	void ProcessButton(ButtonEvent* a_event, void* a_data) {
		if (longPressMode > 0) {
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
			bool isAttackKey = false;
			switch (a_event->device.get()) {
				case INPUT_DEVICE::kKeyboard:
					isAttackKey = keyCode == attackKeyboard;
					break;
				case INPUT_DEVICE::kMouse:
					isAttackKey = keyCode == attackMouse;
					break;
				case INPUT_DEVICE::kGamepad:
					isAttackKey = keyCode == attackGamepad;
					break;
			}

			if (isAttackKey && isHeld) {
				if (longPressMode == 2 && attackWindow) {
					RepeatAttack();
				}
				else {
					return;
				}
			};
			if (keyCode == paKey && isDown && (keyComboPressed || modifierKey < 0)) return;
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

class HookAnimGraphEvent {
public:
	typedef BSEventNotifyControl (HookAnimGraphEvent::* FnReceiveEvent)(BSAnimationGraphEvent* evn, BSTEventSource<BSAnimationGraphEvent>* dispatcher);

	BSEventNotifyControl ReceiveEventHook(BSAnimationGraphEvent* evn, BSTEventSource<BSAnimationGraphEvent>* src) {
		Actor* a = stl::unrestricted_cast<Actor*>(evn->holder);
		if (a) {
			if (!IsRidingHorse(a) && !IsInKillmove(a)) {
				ATTACK_STATE_ENUM currentState = (a->actorState1.meleeAttackState);
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
			if (evn->tag == "JumpUp" || evn->tag == "JumpFall") isJumping = true;
			if (evn->tag == "JumpLandEnd") isJumping = false;
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

using InputEvents = InputEvent*;
class InputEventHandler : public BSTEventSink<InputEvents> {
public:
	virtual BSEventNotifyControl ProcessEvent(const InputEvents* evns, BSTEventSource<InputEvents>* dispatcher) override {
		if (!*evns)
			return BSEventNotifyControl::kContinue;

		if (!p->currentProcess || !p->currentProcess->middleHigh)
			return BSEventNotifyControl::kContinue;

		if (IsRidingHorse(p) || IsInKillmove(p))
			return BSEventNotifyControl::kContinue;
		uint32_t controlFlag = (uint32_t)UserEvents::USER_EVENT_FLAG::kMovement & (uint32_t)UserEvents::USER_EVENT_FLAG::kLooking;
		if (mm->numPausesGame > 0 || ((im->enabledControls.underlying() & controlFlag) != controlFlag) || mm->IsMenuOpen("Dialogue Menu"sv)
			|| p->actorState1.sitSleepState != SIT_SLEEP_STATE::kNormal || (!allowZeroStamina && p->GetActorValue(ActorValue::kStamina) <= 0)) {
			return BSEventNotifyControl::kContinue;
		}

		for (InputEvent* e = *evns; e; e = e->next) {
			switch (e->eventType.get()) {
				case INPUT_EVENT_TYPE::kButton:
				{
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

					if (console && console->uiMovie) {
						if (keyCode == modifierKey && isDown) keyComboPressed = true;
						if (keyCode == modifierKey && isUp) keyComboPressed = false;
						if (keyCode == paKey && isDown) {
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
				}
				break;
			}
		}
		return BSEventNotifyControl::kContinue;
	}
	TES_HEAP_REDEFINE_NEW();
};

class MenuWatcher : public BSTEventSink<MenuOpenCloseEvent> {
public:
	virtual BSEventNotifyControl ProcessEvent(const MenuOpenCloseEvent* evn, BSTEventSource<MenuOpenCloseEvent>* dispatcher) override {
		if (!console && evn->menuName == InterfaceStrings::GetSingleton()->console) {
			console = mm->GetMenu("Console").get();
			if (console)
				logger::info("Console {:#x}", (uintptr_t)console);
		}
		if (evn->menuName == InterfaceStrings::GetSingleton()->loadingMenu && evn->opening) {
			LoadConfigs();
		}
		else if (evn->menuName == InterfaceStrings::GetSingleton()->journalMenu && !evn->opening) {
			GetAttackKeys();
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
	g_message = GetMessagingInterface();
	g_message->RegisterListener([](MessagingInterface::Message* msg) -> void {
		if (msg->type == MessagingInterface::kDataLoaded) {
			BSInputDeviceManager* inputEventDispatcher = BSInputDeviceManager::GetSingleton();
			if (inputEventDispatcher) {
				p = PlayerCharacter::GetSingleton();
				pc = PlayerControls::GetSingleton();
				im = ControlMap::GetSingleton();
				mm = UI::GetSingleton();
				inputString = UserEvents::GetSingleton();
				InputEventHandler* handler = new InputEventHandler();
				inputEventDispatcher->AddEventSink(handler);
				MenuWatcher* mw = new MenuWatcher();
				mm->GetEventSource<MenuOpenCloseEvent>()->AddEventSink(mw);
				UIMessageQueue* ui = UIMessageQueue::GetSingleton();
				InterfaceStrings* uistr = InterfaceStrings::GetSingleton();
				ui->AddMessage(uistr->console, UI_MESSAGE_TYPE::kShow, nullptr);
				ui->AddMessage(uistr->console, UI_MESSAGE_TYPE::kHide, nullptr);
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

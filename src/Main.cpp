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
REL::Relocation<Setting*> fInitialPowerAttackDelay{ RELOCATION_ID(509496,381954) };
REL::Relocation<float*> ptr_engineTime{ RELOCATION_ID(517597, 404125) };
PlayerCharacter* p;
PlayerControls* pc;
BSAudioManager* am;
BGSAction* actionRightPowerAttack;
BGSAction* actionRightAttack;
BGSAction* actionDualPowerAttack;
BGSSoundDescriptorForm* debugPAPressSound;
BGSSoundDescriptorForm* debugPAActivateSound;
UI* mm;
ControlMap* im;
UserEvents* inputString;
CSimpleIniA ini(true, false, false);
uint32_t attackKey[INPUT_DEVICE::kTotal] = { 0xFF, 0xFF, 0xFF };
uint32_t blockKey[INPUT_DEVICE::kTotal] = { 0xFF, 0xFF, 0xFF };
int paKey = 257;
int modifierKey = -1;
bool keyComboPressed = false;

int longPressMode = 2;
bool allowZeroStamina = false;
bool onlyFirstAttack = false;
bool onlyDuringAttack = false;
bool disableBlockDuringAttack = false;

int dualPaKey = 257;
int dualModifierKey = -1;
bool dualKeyComboPressed = false;
bool dualOnlyFirstAttack = false;
bool dualOnlyDuringAttack = false;

bool notifyWindow = false;
float notifyDuration = 0.1f;
TESEffectShader* notifyFX = nullptr;

bool queuePA = false;
float queuePAExpire = 0.2f;
BGSAction* queuePAAction = nullptr;

bool isAttacking = false;
bool attackWindow = false;
float paQueueTime = 0.f;

bool debugPAPress = false;
bool debugPAActivate = false;
std::string MCOWinOpen = "MCO_WinOpen";
std::string MCOWinClose = "MCO_WinOpen";
std::string MCOPowerWinOpen = "MCO_PowerWinOpen";
std::string MCOPowerWinClose = "MCO_PowerWinClose";

std::string SplitString(const std::string str, const std::string delimiter, std::string& remainder) {
	std::string ret;
	size_t i = str.find(delimiter);
	if (i == std::string::npos) {
		ret = str;
		remainder = "";
		return ret;
	}

	ret = str.substr(0, i);
	remainder = str.substr(i + 1);
	return ret;
}

TESForm* GetFormFromMod(std::string modname, uint32_t formid) {
	if (!modname.length() || !formid)
		return nullptr;
	TESDataHandler* dh = TESDataHandler::GetSingleton();
	TESFile* modFile = nullptr;
	for (auto it = dh->files.begin(); it != dh->files.end(); ++it) {
		TESFile* f = *it;
		if (strcmp(f->fileName, modname.c_str()) == 0) {
			modFile = f;
			break;
		}
	}
	if (!modFile)
		return nullptr;
	uint8_t modIndex = modFile->compileIndex;
	uint32_t id = formid;
	if (modIndex < 0xFE) {
		id |= ((uint32_t)modIndex) << 24;
	}
	else {
		uint16_t lightModIndex = modFile->smallFileCompileIndex;
		if (lightModIndex != 0xFFFF) {
			id |= 0xFE000000 | (uint32_t(lightModIndex) << 12);
		}
	}
	return TESForm::LookupByID(id);
}

template <class T = TESForm>
T* GetFormFromConfigString(const std::string str) {
	std::string formIDstr;
	std::string plugin = SplitString(str, "|", formIDstr);
	if (formIDstr.length() != 0) {
		uint32_t formID = std::stoi(formIDstr, 0, 16);
		T* form = skyrim_cast<T*, TESForm>(GetFormFromMod(plugin, formID));
		if (form->formType == T::FORMTYPE)
			return form;
	}
	return nullptr;
}

bool IsRidingHorse(Actor* a) {
	return (a->AsActorState()->actorState1.sitSleepState == SIT_SLEEP_STATE::kRidingMount);
}

bool IsInKillmove(Actor* a) {
	return a->GetActorRuntimeData().boolFlags.all(Actor::BOOL_FLAGS::kIsInKillMove);
}

bool IsPAQueued() {
	return *ptr_engineTime - paQueueTime <= queuePAExpire;
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

void PlayDebugSound(BGSSoundDescriptorForm* sound) {
	BSSoundHandle handle;
	am->BuildSoundDataFromDescriptor(handle, sound->soundDescriptor);
	handle.SetVolume(1.f);
	handle.SetObjectToFollow(p->Get3D());
	handle.Play();
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
		bool succ = func(data.get());
		if (succ) {
			if (debugPAActivate && action->formID != 0x13005) {
				PlayDebugSound(debugPAActivateSound);
			}
			if (IsPAQueued() && action == queuePAAction) {
				paQueueTime = 0.f;
			}
		}
		else {
			if (queuePA) {
				paQueueTime = *ptr_engineTime;
				queuePAAction = action;
			}
		}
	});
}

//For Jumping Attack/Vanguard
void AltPowerAttack() {
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	PerformAction(actionRightPowerAttack, p);
}

void PowerAttack() {
	if (debugPAPress)
		PlayDebugSound(debugPAPressSound);
	bool isJumping = false;
	p->GetGraphVariableBool("bInJumpState", isJumping);
	bool isBlocking = false;
	p->GetGraphVariableBool("IsBlocking", isBlocking);
	if (isJumping || isBlocking) {
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
}

//Resets some variables after load
void ResetVariables() {
	paQueueTime = 0.f;
	isAttacking = false;
}

//Load configs from the ini file
void LoadConfigs() {
	std::string path = "Data\\MCM\\Config\\OCPA\\settings.ini";
	if (std::filesystem::exists("Data\\MCM\\Settings\\OCPA.ini")) {
		path = "Data\\MCM\\Settings\\OCPA.ini";
	}
	SI_Error result = ini.LoadFile(path.c_str());
	if (result >= 0) {
		paKey = std::stoi(ini.GetValue("General", "iKeycode", "257"));
		modifierKey = std::stoi(ini.GetValue("General", "iModifierKey", "-1"));
		longPressMode = std::stoi(ini.GetValue("General", "iLongPressMode", "2"));
		allowZeroStamina = std::stoi(ini.GetValue("General", "bAllowZeroStamina", "1")) > 0;
		onlyFirstAttack = std::stoi(ini.GetValue("General", "bSkipModifierDuringCombo", "0")) > 0;
		onlyDuringAttack = std::stoi(ini.GetValue("General", "bOnlyDuringAttack", "1")) > 0;
		disableBlockDuringAttack = std::stoi(ini.GetValue("General", "bDisableBlockDuringAttack", "1")) > 0;
		bool disableBlockKey = std::stoi(ini.GetValue("General", "bDisableBlockKey", "0")) > 0;

		dualPaKey = std::stoi(ini.GetValue("DualAttack", "iKeycode", "257"));
		dualModifierKey = std::stoi(ini.GetValue("DualAttack", "iModifierKey", "42"));
		dualOnlyFirstAttack = std::stoi(ini.GetValue("DualAttack", "bSkipModifierDuringCombo", "0")) > 0;
		dualOnlyDuringAttack = std::stoi(ini.GetValue("DualAttack", "bOnlyDuringAttack", "0")) > 0;

		notifyWindow = std::stoi(ini.GetValue("MCO", "bNotifyAttackWindow", "1")) > 0;
		notifyDuration = std::stof(ini.GetValue("MCO", "fNotifyDuration", "0.05"));
		std::string notifyFXStr = ini.GetValue("MCO", "sNotifyEffect", "OCPA.esl|0xD63");
		queuePA = std::stoi(ini.GetValue("MCO", "bQueuePowerAttack", "1")) > 0;
		queuePAExpire = std::stof(ini.GetValue("MCO", "fQueueExpire", "0.2"));

		debugPAPress = std::stoi(ini.GetValue("Debug", "bNotifyPress", "0")) > 0;
		debugPAActivate = std::stoi(ini.GetValue("Debug", "bNotifyActivate", "0")) > 0;
		float powerAttackCooldown = std::stof(ini.GetValue("Debug", "fPowerAttackCooldown", "0"));
		MCOWinOpen = ini.GetValue("MCO", "sMCOWinOpen", "MCO_WinOpen");
		MCOWinClose = ini.GetValue("MCO", "sMCOWinClose", "MCO_WinClose");
		MCOPowerWinOpen = ini.GetValue("MCO", "sMCOPowerWinOpen", "MCO_PowerWinOpen");
		MCOPowerWinClose = ini.GetValue("MCO", "sMCOPowerWinClose", "MCO_PowerWinClose");

		notifyFX = GetFormFromConfigString<TESEffectShader>(notifyFXStr);

		for (auto it = GameSettingCollection::GetSingleton()->settings.begin(); it != GameSettingCollection::GetSingleton()->settings.end(); ++it) {
			if (strcmp((*it).first, "fPowerAttackCoolDownTime") == 0) {
				(*it).second->data.f = powerAttackCooldown;
			}
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
	else {
		logger::critical("Failed to load config.");
	}
	ini.Reset();
}

//Fired when the user presses the attack or block key
class HookAttackBlockHandler {
public:
	typedef void (HookAttackBlockHandler::* FnProcessButton) (ButtonEvent*, void*);
	void ProcessButton(ButtonEvent* a_event, void* a_data) {
		TESObjectWEAP* weap = reinterpret_cast<TESObjectWEAP*>(p->GetEquippedObject(false));
		if (!IsRidingHorse(p) && !IsInKillmove(p) && (!weap || (!weap->IsBow() && !weap->IsStaff() && !weap->IsCrossbow()))) {
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
			bool isHeld = a_event->value != 0 && timer > 0;
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
						if (timer >= fInitialPowerAttackDelay->GetFloat())
							return;
					}
				};
			}

			//To provide consistency, don't block even if there's no PA combo connected to the attack
			if (isBlockKey && isAttacking)
				if (onlyDuringAttack || disableBlockDuringAttack)
					return;
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
				if (currentState >= ATTACK_STATE_ENUM::kDraw && currentState <= ATTACK_STATE_ENUM::kBash) {
					isAttacking = true;
				}
				else {
					isAttacking = false;
				}
			}
			else {
				isAttacking = false;
			}
			if (evn->tag == MCOWinOpen.c_str()) {
				attackWindow = true;
				if (notifyWindow) {
					if (notifyFX) {
						p->InstantiateHitShader(notifyFX, notifyDuration);
					}
					else {
						logger::critical("NotifyEffect is not valid!");
					}
				}
			}
			else if (evn->tag == MCOWinClose.c_str()) attackWindow = false;
			else if (IsPAQueued() && (evn->tag == MCOPowerWinOpen.c_str() || evn->tag == MCOPowerWinClose.c_str())) {
				PerformAction(queuePAAction, p);
			}
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
					bool isHeld = a_event->value != 0 && timer > 0;
					bool isUp = a_event->value == 0 && timer != 0;

					bool wantPowerAttack = false;
					bool wantDualPowerAttack = false;

					if (keyCode == modifierKey && isDown) keyComboPressed = true;
					if (keyCode == modifierKey && isUp) keyComboPressed = false;
					//Simplified logic for power attacks
					//onlyDuringAttack? -> check isAttacking
					//onlyFirstAttack? -> check keyComboPressed if it's not the first attack
					//no modifier key? -> just power attack whenever the key was pressed
					if (keyCode == paKey) {
						if (isDown) {
							if ((isAttacking && onlyDuringAttack) || !onlyDuringAttack) {
								if (modifierKey >= 2) {
									if (onlyFirstAttack) {
										if ((!isAttacking && keyComboPressed) || isAttacking) {
											wantPowerAttack = true;
										}
									}
									else {
										if (keyComboPressed) {
											wantPowerAttack = true;
										}
									}
								}
								else {
									wantPowerAttack = true;
								}
							}
						}
					}

					//Copypasta for dual attack
					if (keyCode == dualModifierKey && isDown) dualKeyComboPressed = true;
					if (keyCode == dualModifierKey && isUp) dualKeyComboPressed = false;
					if (keyCode == dualPaKey) {
						if (isDown) {
							if ((isAttacking && dualOnlyDuringAttack) || !dualOnlyDuringAttack) {
								if (dualModifierKey >= 2) {
									if (dualOnlyFirstAttack) {
										if ((!isAttacking && dualKeyComboPressed) || isAttacking) {
											wantDualPowerAttack = true;
										}
									}
									else {
										if (dualKeyComboPressed) {
											wantDualPowerAttack = true;
										}
									}
								}
								else {
									wantDualPowerAttack = true;
								}
							}
						}
					}

					if (wantPowerAttack && wantDualPowerAttack) {
						if (modifierKey >= 2 && dualModifierKey < 2) {
							PowerAttack();
						}
						else if (modifierKey < 2 && dualModifierKey >= 2) {
							DualPowerAttack();
						}
						else {
							PowerAttack();
						}
					}
					else {
						if (wantPowerAttack) {
							PowerAttack();
						}
						else if (wantDualPowerAttack) {
							DualPowerAttack();
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
			ResetVariables();
			LoadConfigs();
		}
		//Load key settings after closing journal menu
		else if (evn->menuName == InterfaceStrings::GetSingleton()->journalMenu && !evn->opening) {
			GetKeySettings();
			LoadConfigs();
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
				am = BSAudioManager::GetSingleton();
				actionRightAttack = (BGSAction*)TESForm::LookupByID(0x13005);
				actionRightPowerAttack = (BGSAction*)TESForm::LookupByID(0x13383);
				actionDualPowerAttack = (BGSAction*)TESForm::LookupByID(0x2E2F7);
				debugPAPressSound = (BGSSoundDescriptorForm*)TESForm::LookupByID(0x3F3F8);
				debugPAActivateSound = (BGSSoundDescriptorForm*)TESForm::LookupByID(0x3F3FA);
				inputString = UserEvents::GetSingleton();
				InputEventHandler* handler = new InputEventHandler();
				inputEventDispatcher->AddEventSink(handler);
				MenuWatcher* mw = new MenuWatcher();
				mm->GetEventSource<MenuOpenCloseEvent>()->AddEventSink(mw);
				HookAnimGraphEvent::Hook();
				HookAttackBlockHandler::Hook();
				LoadConfigs();
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

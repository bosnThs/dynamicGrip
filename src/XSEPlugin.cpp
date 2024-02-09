#include <SimpleIni.h>
#include <shared_mutex>

bool	isSwitching = false;
bool    isQueuedGripSwitch = false;
RE::TESForm* previouLeftWeapon;

RE::BGSEquipSlot* leftHandSlot;
RE::BGSEquipSlot* rightHandSlot;
RE::BGSEquipSlot* shieldSlot;
RE::BGSEquipSlot* twoHandSlot;
RE::SpellItem*    dgSpell;
RE::BGSSoundDescriptorForm* switchIn;
RE::BGSSoundDescriptorForm* switchOut;

RE::WEAPON_TYPE originalRightWeapon;
RE::WEAPON_TYPE originalLeftWeapon;

//RE::PlayerCharacter* player;

const int DEFAULTGRIPMODE = 0;
const int TWOHANDEDGRIPMODE = 1;
const int ONEHANDEDGRIPMODE = 2;
const int DUALWEILDGRIPMODE = 3;

int gripMode;

std::uint16_t keyboardKey, keyboardMod, gamepadKey, gamepadMod;
char*         reqPerkEditorID_1H;
char*		  reqPerkEditorID_2H;
RE::BGSPerk*  reqPerk1H;
RE::BGSPerk*  reqPerk2H;

void loadIni()
{
	CSimpleIniA ini;
	ini.SetUnicode();
	ini.LoadFile(L"Data\\SKSE\\Plugins\\dynamicGrip.ini");

	keyboardKey = (uint16_t)ini.GetDoubleValue("settings", "iKeyboardKey", 34);
	keyboardMod = (uint16_t)ini.GetDoubleValue("settings", "iKeyboardModifier", 0);
	gamepadKey = (uint16_t) ini.GetDoubleValue("settings", "iGamePadKey", 9);		//LT
	gamepadMod = (uint16_t)ini.GetDoubleValue("settings", "iGamePadMod", 4096);  //A
	
	auto s = (char*)ini.GetValue("settings", "sRequiredPerk1H", "");
	reqPerkEditorID_1H = new char[strlen(s) + 1];
	memcpy(reqPerkEditorID_1H, s, strlen(s) + 1);

	s = (char*)ini.GetValue("settings", "sRequiredPerk2H", "");
	reqPerkEditorID_2H = new char[strlen(s) + 1];
	memcpy(reqPerkEditorID_2H, s, strlen(s) + 1);
}

struct mainFunctions
{
	static void Hook()
	{
		//REL::Relocation<std::uintptr_t> AttackBlockHandlerVtbl{ RE::VTABLE_AttackBlockHandler[0] };
		//_CanProcessAttackBlock = AttackBlockHandlerVtbl.write_vfunc(0x1, CanProcessAttackBlock);

		REL::Relocation<std::uintptr_t> ShoutHandlerVtbl{ RE::VTABLE_ShoutHandler[0] };
		_CanProcessShout = ShoutHandlerVtbl.write_vfunc(0x1, CanProcessShout);

		REL::Relocation<std::uintptr_t> PlayerCharacterVtbl{ RE::VTABLE_PlayerCharacter[0] };
		_OnItemEquipped = PlayerCharacterVtbl.write_vfunc(0xb2, OnItemEquipped);

		REL::Relocation<std::uintptr_t> StandardItemDataVtbl{ RE::VTABLE_StandardItemData[0] };
		_GetEquipState = StandardItemDataVtbl.write_vfunc(0x3, GetEquipState);
	}

	static std::uint32_t GetEquipState(RE::StandardItemData* a_this)
	{
		std::uint32_t a_result = _GetEquipState(a_this);
		if (a_result > 1) {  //2 -left 3-right 4-left/right
			RE::NiPointer<RE::TESObjectREFR> refr;
			if (RE::LookupReferenceByHandle(a_this->owner, refr) && refr->IsPlayerRef()) 
			{
				auto eqObj = a_this->objDesc->object;
				if (eqObj) 
				{
					if (gripMode == TWOHANDEDGRIPMODE)
						return 4;

					if (a_result == 4 && gripMode != DEFAULTGRIPMODE && eqObj->IsWeapon() && isTwoHanded(eqObj->As<RE::TESObjectWEAP>()))
						return 3;
				}
			}
		}
		return a_result;
	}
	static inline REL::Relocation<decltype(GetEquipState)> _GetEquipState;

	static int getObjectType(RE::TESForm* form)
	{
		if (!form)
			return 0;

		if (form->IsWeapon())
		{
			if (form->As<RE::TESObjectWEAP>()->IsCrossbow())
				return 12;		//xbow type is 9 same as spells but graph vars use the value 12
			return (int)form->As<RE::TESObjectWEAP>()->GetWeaponType();
		}

		//spells
		if (form->IsMagicItem())
			return 9;

		//shield
		if (form->IsArmor())
			return 10;

		//torch
		return 11;
	}

	static void setHandAnim(RE::Actor* a_actor, bool left)
	{
		RE::BSFixedString s = "iLeftHandType";
		if (!left)
			s = "iRightHandType";
		a_actor->SetGraphVariableInt(s, getObjectType(a_actor->GetEquippedObject(left)));
	}

	static void OnItemEquipped(RE::PlayerCharacter* a_this, bool anim)
	{
		if ((gripMode == ONEHANDEDGRIPMODE || gripMode == DUALWEILDGRIPMODE))  // && c== 1)
		{
			auto rightHand = a_this->GetEquippedObject(false);
			auto leftHand = a_this->GetEquippedObject(true);

			if (rightHand && rightHand->IsWeapon()) {
				originalRightWeapon = rightHand->As<RE::TESObjectWEAP>()->GetWeaponType();
				if (mainFunctions::isTwoHanded(rightHand->As<RE::TESObjectWEAP>()))
					rightHand->As<RE::TESObjectWEAP>()->weaponData.animationType = RE::WEAPON_TYPE::kOneHandSword;
			}

			if (leftHand && leftHand->IsWeapon() && rightHand && leftHand->GetFormID() != rightHand->GetFormID()) {
				originalLeftWeapon = leftHand->As<RE::TESObjectWEAP>()->GetWeaponType();
				if (mainFunctions::isTwoHanded(leftHand->As<RE::TESObjectWEAP>()))
					leftHand->As<RE::TESObjectWEAP>()->weaponData.animationType = RE::WEAPON_TYPE::kOneHandSword;
			}

			_OnItemEquipped(a_this, anim);

			if (rightHand && rightHand->IsWeapon()) {
				rightHand->As<RE::TESObjectWEAP>()->weaponData.animationType = originalRightWeapon;
			}

			if (leftHand && leftHand->IsWeapon() && rightHand && leftHand->GetFormID() != rightHand->GetFormID()) {
				leftHand->As<RE::TESObjectWEAP>()->weaponData.animationType = originalLeftWeapon;
			}

			return;
		}
		_OnItemEquipped(a_this, anim);
		//reset anim vars to the correct weapon types
		//it just works
		setHandAnim(a_this, false);
		setHandAnim(a_this, true);
	}
	static inline REL::Relocation<decltype(OnItemEquipped)> _OnItemEquipped;

	static bool CanProcessAttackBlock(RE::AttackBlockHandler* a_this, RE::InputEvent* a_event)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto s = a_event->QUserEvent();
		if (player->AsActorState()->IsWeaponDrawn() && s == "GripSwitch" && !a_event->AsButtonEvent()->IsPressed() || inputHandler(a_event))
			if (gripSwitch(player))
				return false;

		return _CanProcessAttackBlock(a_this, a_event);
	}
	static inline REL::Relocation<decltype(CanProcessAttackBlock)> _CanProcessAttackBlock;

	static bool CanProcessShout(RE::ShoutHandler* a_this, RE::InputEvent* a_event)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto s = a_event->QUserEvent();
		if (player->AsActorState()->IsWeaponDrawn() && s == "GripSwitch" && !a_event->AsButtonEvent()->IsPressed() || inputHandler(a_event)) {
			if (gripSwitch(player)) 
			{
				isQueuedGripSwitch = false;
				return false;
			}
		}

		return _CanProcessShout(a_this, a_event);
	}
	static inline REL::Relocation<decltype(CanProcessShout)> _CanProcessShout;
	/*
	static void queueGripSwitch(RE::InputEvent* a_event)
	{
		static auto event = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kNone, "GripSwitch", -1, 1, 0);
		//RE::BSInputEventQueue::GetSingleton()->PushOntoInputQueue(event);
	}	
	*/
	static bool inputHandler(RE::InputEvent* a_event)
	{
		if (a_event && a_event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
			bool mk = false;
			bool mg = false;
			bool kk = false;
			bool gk = false;

			if (keyboardMod == 0)
				mk = true;
			if (gamepadMod == 0)
				mg = true;

			do {
				auto bEvent = a_event->AsButtonEvent();
				if (bEvent) {
					std::int32_t key;

					// Mouse
					if (a_event->device.get() == RE::INPUT_DEVICE::kMouse) {
						key = ButtonEventToDXScanCode(RE::INPUT_DEVICE::kMouse, bEvent);
					}
					// Gamepad
					//else if (a_event->device.get() == RE::INPUT_DEVICE::kGamepad) {
					//	key = ButtonEventToDXScanCode(RE::INPUT_DEVICE::kGamepad, bEvent);
					//}
					// Keyboard
					else
						key = ButtonEventToDXScanCode(RE::INPUT_DEVICE::kKeyboard, bEvent);

					if (key == keyboardKey && !bEvent->IsPressed())
						kk = true;
					//if (key == gamepadKey && !bEvent->IsPressed())
					//	gk = true;
					if (key == keyboardMod)
						mk = true;
					//if (key == gamepadMod)
					//	mg = true;
				}
				a_event = a_event->next;
			} while (a_event);

			if (kk && mk || gk && mg)
				return true;
		}
		return false;
	}

	static std::int32_t ButtonEventToDXScanCode(RE::INPUT_DEVICE device, RE::ButtonEvent* event)
	{
		switch (device) {
		case RE::INPUT_DEVICE::kKeyboard:
			return std::int32_t(event->GetIDCode());
		case RE::INPUT_DEVICE::kMouse:
			return std::int32_t(event->GetIDCode() + 256);
		case RE::INPUT_DEVICE::kGamepad:
			{
				using GB = RE::BSWin32GamepadDevice::Key;
				const auto button = static_cast<GB>(event->GetIDCode());
				switch (button) {
				case GB::kUp:
					return 266;
				case GB::kDown:
					return 267;
				case GB::kLeft:
					return 268;
				case GB::kRight:
					return 269;
				case GB::kStart:
					return 270;
				case GB::kBack:
					return 271;
				case GB::kLeftThumb:
					return 272;
				case GB::kRightThumb:
					return 273;
				case GB::kLeftShoulder:
					return 274;
				case GB::kRightShoulder:
					return 275;
				case GB::kA:
					return 276;
				case GB::kB:
					return 277;
				case GB::kX:
					return 278;
				case GB::kY:
					return 279;
				case GB::kLeftTrigger:
					return 280;
				case GB::kRightTrigger:
					return 281;
				}
			}
		}
		return 0;
	}

	static bool gripSwitch(RE::PlayerCharacter* player)
	{
		//auto player = RE::PlayerCharacter::GetSingleton();
		if (player->AsActorState()->IsWeaponDrawn()) {
			RE::ATTACK_STATE_ENUM currentState = (player->AsActorState()->actorState1.meleeAttackState);
			if (currentState != RE::ATTACK_STATE_ENUM::kNone && !isSwitching)
				return false;

			auto rightHand = player->GetEquippedObject(false);
			auto leftHand = player->GetEquippedObject(true);

			//only viable weapons to grip switch
			if (rightHand && rightHand->IsWeapon() && canSwitch(rightHand->As<RE::TESObjectWEAP>())) {
				auto rightWeapon = rightHand->As<RE::TESObjectWEAP>();

				isSwitching = true;
				int newGrip;
				switch (gripMode) {
				case DEFAULTGRIPMODE:
					if (isOneHanded(rightWeapon)) {
						if (!checkPerk(player, false))
							return false;
						//turn weapon in main hand into a 2h and remove left hand
						newGrip = TWOHANDEDGRIPMODE;

						//unequip left weapon
						previouLeftWeapon = leftHand;  //save previous left weapon/shield/spell even if empty
						if (leftHand)
							unequipLeftSlot(player, true);

					} else {
						if (!checkPerk(player, true))
							return false;

						//is twoHanded
						newGrip = ONEHANDEDGRIPMODE;
						//automatically equip a weapon in left hand
						if (previouLeftWeapon) 
							equipLeftSlot(player, previouLeftWeapon);
					}
					break;
				case TWOHANDEDGRIPMODE:
					//requip previous leftHand weapon
					newGrip = DEFAULTGRIPMODE;

					if (previouLeftWeapon) 
						equipLeftSlot(player, previouLeftWeapon);


					break;
				case ONEHANDEDGRIPMODE:
				case DUALWEILDGRIPMODE:
					//unequip left weapon
					previouLeftWeapon = leftHand;  //save previous left weapon/shield/spell
					if (leftHand)
						unequipLeftSlot(player, true);

				default:
					newGrip = DEFAULTGRIPMODE;
					break;
				};

				player->NotifyAnimationGraph("GripSwitchEvent");
				isSwitching = false;
				toggleGrip(newGrip,true);
				/*
				std::thread([]() {
					std::this_thread::sleep_for(std::chrono::milliseconds(300));
					SKSE::GetTaskInterface()->AddTask([]() {
						isSwitching = false;
					});
				}).detach();
				*/
			}
		}
		return true;
	}

	static bool canSwitch(RE::TESObjectWEAP* weap)
	{
		if (weap->IsOneHandedSword() || weap->IsOneHandedDagger() || weap->IsOneHandedAxe() || weap->IsOneHandedMace() || weap->IsTwoHandedSword() || weap->IsTwoHandedAxe())
			return true;
		return false;
	}

	static bool checkPerk(RE::Actor* a_player, bool twoHand)
	{
		if (twoHand) 
		{
			if (!reqPerk2H || a_player->HasPerk(reqPerk2H))
				return true;
			return false;
		} else {
			if (!reqPerk1H || a_player->HasPerk(reqPerk1H))
				return true;
			return false;
		}
	}

	static bool isOneHanded(RE::TESObjectWEAP* weap)
	{
		if (weap->IsOneHandedSword() || weap->IsOneHandedDagger() || weap->IsOneHandedAxe() || weap->IsOneHandedMace())
			return true;
		return false;
	}

	static bool isTwoHanded(RE::TESObjectWEAP* weap)
	{
		if (weap->IsTwoHandedSword() || weap->IsTwoHandedAxe())
			return true;
		return false;
	}

	static void toggleGrip(int mode, bool bPlaySound = false)
	{
		gripMode = mode;
		//gripGlobal->value = (float)mode;

		auto player = RE::PlayerCharacter::GetSingleton();
		player->SetGraphVariableInt("iDynamicGripMode", gripMode);

		if (bPlaySound) 
		{
			if (mode == DEFAULTGRIPMODE)
				playSound(player, switchOut);
			else
				playSound(player, switchIn);
		}
	}

	static void playSound(RE::Actor* a, RE::BGSSoundDescriptorForm* a_descriptor)
	{
		if (!a_descriptor)
			return;

		RE::BSSoundHandle handle;
		handle.soundID = a_descriptor->GetFormID();
		handle.assumeSuccess = false;
		*(uint32_t*)&handle.state = 0;

		handle.SetPosition(a->GetPosition());
		handle.SetVolume(1);

		auto sm = RE::BSAudioManager::GetSingleton();
		sm->BuildSoundDataFromDescriptor(handle, a_descriptor);
		sm->Play(a_descriptor);
	}

	static void equipLeftSlot(RE::PlayerCharacter* player, RE::TESForm* a_weapon)
	{
		//check if weapon/armor is still in inventory
		auto inventory = player->GetInventory();
		for (auto& [item, data] : inventory) 
		{
			const auto& [count, entry] = data;
			if (count > 0 && item->GetFormID() == a_weapon->GetFormID() || a_weapon->IsMagicItem()) 
			{
				RE::BGSEquipSlot* slot;
				if (a_weapon->IsArmor())
					slot = shieldSlot;
				else
					slot = leftHandSlot;

				auto* task = SKSE::GetTaskInterface();
				auto  equipManager = RE::ActorEquipManager::GetSingleton();
				task->AddTask([=]() {
					if (a_weapon->IsMagicItem())
						equipManager->EquipSpell(player, a_weapon->As<RE::SpellItem>(), slot);
					else
						equipManager->EquipObject(player, a_weapon->As<RE::TESBoundObject>(), a_weapon->As<RE::ExtraDataList>(), 1, slot, true, false, true, true);
				});
				return;
			}
		}
	}

	static void unequipLeftSlot(RE::PlayerCharacter* player, bool leftH, bool now = false)
	{
		auto form = player->GetEquippedObject(leftH);
		if (!form)
			return;

		RE::BGSEquipSlot* slot;
		if (form->IsArmor())  //shield check
			slot = shieldSlot;
		else
			slot = leftHandSlot;

		if (now) 
			unequipSlotNOW(player, slot);
		 else 
			unequipSlot(player, slot);
	}

	static void unequipSlotNOW(RE::PlayerCharacter* player, RE::BGSEquipSlot* slot)
	{
		auto* form = RE::TESForm::LookupByID<RE::TESForm>(0x00020163);  //dummydagger
		if (!form) {
			return;
		}
		auto* proxy = form->As<RE::TESObjectWEAP>();
		auto* equip_manager = RE::ActorEquipManager::GetSingleton();
		equip_manager->EquipObject(player, proxy, nullptr, 1, slot, false, true, false);
		equip_manager->UnequipObject(player, proxy, nullptr, 1, slot, false, true, false);
		return;
	}

	static void unequipSlot(RE::PlayerCharacter* player, RE::BGSEquipSlot* slot)
	{
		auto* task = SKSE::GetTaskInterface();
		if (task) {
			auto* form = RE::TESForm::LookupByID<RE::TESForm>(0x00020163); //dummydagger
			if (!form) {
				return;
			}
			auto* proxy = form->As<RE::TESObjectWEAP>();
			auto* equip_manager = RE::ActorEquipManager::GetSingleton();
			task->AddTask([=]() { equip_manager->EquipObject(player, proxy, nullptr, 1, slot, false, true, false); });
			task->AddTask([=]() { equip_manager->UnequipObject(player, proxy, nullptr, 1, slot, false, true, false); });
			return;
		}
	}
};

namespace controlMap
{
	struct controlMapHook
	{
		static void thunk(RE::ControlMap* a_controlMap)
		{
			func(a_controlMap);

			auto gskey = (std::uint16_t)a_controlMap->GetMappedKey("GripSwitch", RE::INPUT_DEVICE::kGamepad, RE::ControlMap::InputContextID::kGameplay);
			if (gskey == 0xFF) 
				setGamePadButton(a_controlMap);

			//gskey = (std::uint16_t)a_controlMap->GetMappedKey("GripSwitch", RE::INPUT_DEVICE::kKeyboard, RE::ControlMap::InputContextID::kGameplay);
			//if (gskey == 0xFF)
			//	setKeyboardButton(a_controlMap);

		}
		static inline REL::Relocation<decltype(thunk)> func;

		static void setGamePadButton(RE::ControlMap* a_controlMap)
		{
			RE::ControlMap::UserEventMapping n;
			n.eventID = "GripSwitch";
			//n.inputKey = (std::uint16_t)a_controlMap->GetMappedKey("Right Attack/Block", RE::INPUT_DEVICE::kGamepad, RE::ControlMap::InputContextID::kGameplay);
			n.inputKey = gamepadKey;	//(std::uint16_t) a_controlMap->GetMappedKey("Shout", RE::INPUT_DEVICE::kGamepad, RE::ControlMap::InputContextID::kGameplay);
			n.modifier = gamepadMod;	//(std::uint16_t) a_controlMap->GetMappedKey("Activate", RE::INPUT_DEVICE::kGamepad, RE::ControlMap::InputContextID::kGameplay);
			n.remappable = false;
			n.linked = false;
			n.pad14 = 0;
			n.userEventGroupFlag = RE::UserEvents::USER_EVENT_FLAG::kFighting;
			a_controlMap->controlMap[RE::ControlMap::InputContextID::kGameplay]->deviceMappings[RE::INPUT_DEVICES::kGamepad].push_back(n);
		}
	};

	static void installHooks()
	{
		if (REL::Module::IsAE()) {
			constexpr std::array locations{
				std::make_pair<std::uint64_t, std::size_t>(53270, 0x17),
				std::make_pair<std::uint64_t, std::size_t>(53299, 0x17),
				std::make_pair<std::uint64_t, std::size_t>(68534, 0x165),
				std::make_pair<std::uint64_t, std::size_t>(68540, 0x266),
			};

			for (const auto& [id, offset] : locations) {
				REL::Relocation<std::uintptr_t> target(REL::ID(id), offset);
				stl::write_thunk_call<controlMapHook>(target.address());
			}
		} else {
			constexpr std::array locations{
				std::make_pair<std::uint64_t, std::size_t>(52374, 0x17),
				std::make_pair<std::uint64_t, std::size_t>(52400, 0x17),
				std::make_pair<std::uint64_t, std::size_t>(67234, 0x113),
				std::make_pair<std::uint64_t, std::size_t>(67240, 0x17B),
			};

			for (const auto& [id, offset] : locations) {
				REL::Relocation<std::uintptr_t> target(REL::ID(id), offset);
				stl::write_thunk_call<controlMapHook>(target.address());
			}
		}
	}
}

namespace Events
{
	class OnEquipEventHandler : public RE::BSTEventSink<RE::TESEquipEvent>
	{
	public:
		static OnEquipEventHandler* GetSingleton()
		{
			static OnEquipEventHandler singleton;
			return &singleton;
		}

		static void RegisterListener()
		{
			RE::ScriptEventSourceHolder* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
			eventHolder->AddEventSink(OnEquipEventHandler::GetSingleton());
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>* ) override
		{
			if (!a_event || !a_event->actor || !a_event->actor->IsPlayerRef())
				return RE::BSEventNotifyControl::kContinue;

			auto ui = RE::UI::GetSingleton();
			if (ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) 
			{
				auto invMenu = ui->GetMenu<RE::InventoryMenu>(RE::InventoryMenu::MENU_NAME);
				invMenu.get()->GetRuntimeData().itemList->Update();
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			auto rightHand = player->GetEquippedObject(false);
			auto leftHand = player->GetEquippedObject(true);

			auto* form = RE::TESForm::LookupByID(a_event->baseObject);

			if (a_event->equipped) 
			{
				if (previouLeftWeapon && previouLeftWeapon->GetFormID() == form->GetFormID())  //remove cached left-weapon to prevent duping
					previouLeftWeapon = nullptr;

				//if (rightHand && rightHand->IsWeapon() && rightHand->GetFormID() == form->GetFormID() && mainFunctions::isTwoHanded(rightHand->As<RE::TESObjectWEAP>()) && rightHand != leftHand) //unequip left hand if equipping a 2hander in right hand
				//{
				//	mainFunctions::unequipLeftSlot(player, true, true);
				//	return RE::BSEventNotifyControl::kContinue;
				//}

				if (leftHand && leftHand->GetFormID() == form->GetFormID() || rightHand && rightHand->GetFormID() == form->GetFormID()) 
				{
					switch (gripMode) {
						case TWOHANDEDGRIPMODE:  //main-hand base is 1H
							mainFunctions::toggleGrip(DEFAULTGRIPMODE);
							break;

						//case DEFAULTGRIPMODE:
						//case ONEHANDEDGRIPMODE:
						//case DUALWEILDGRIPMODE:
						default:
							//equipped 2h in right hand
							if (rightHand && rightHand->IsWeapon() && mainFunctions::isTwoHanded(rightHand->As<RE::TESObjectWEAP>()) && mainFunctions::checkPerk(player, true)) 
							{
								if (leftHand) 
								{
									int newGrip = ONEHANDEDGRIPMODE;

									if (leftHand->IsWeapon())
										newGrip = DUALWEILDGRIPMODE;

									if (gripMode == DEFAULTGRIPMODE)
										player->NotifyAnimationGraph("GripSwitchEvent");

									mainFunctions::toggleGrip(newGrip);
								}
								return RE::BSEventNotifyControl::kContinue;
							}
							break;
					};
				}
			} else {
				//unequipped main-hand weapon
				if ((!rightHand || !leftHand) && (gripMode == ONEHANDEDGRIPMODE || gripMode == DUALWEILDGRIPMODE)) 
				{  
					player->NotifyAnimationGraph("GripSwitchEvent");
					mainFunctions::toggleGrip(DEFAULTGRIPMODE);
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		OnEquipEventHandler() = default;
	};

	inline static void Register()
	{
		OnEquipEventHandler::RegisterListener();
	}
}

namespace Hooks
{
	struct getEquipTypeIconDisplay
	{
		static std::int64_t thunk(RE::TESForm* form)
		{
			//no gameplay effect only changes the r/l icon of equipped weapons
			//no actor info here so icon might not be accurate when trading with followers etc
			auto rightHandType = RE::TESForm::LookupByID<RE::BGSEquipType>(0x00020163);  //dummydagger
			auto twoHandType = RE::TESForm::LookupByID<RE::BGSEquipType>(0x6a0b8);       //dummygreatsword
			//dupe 2h equipslot for a 1h
			if (form && form->IsWeapon()) {
				if (mainFunctions::isTwoHanded(form->As<RE::TESObjectWEAP>())) {
					if (gripMode == DEFAULTGRIPMODE)
							return (std::int64_t)twoHandType;
					else
							return (std::int64_t)rightHandType;
				}

				if (mainFunctions::isOneHanded(form->As<RE::TESObjectWEAP>()) && (gripMode == TWOHANDEDGRIPMODE))
					return (std::int64_t)twoHandType;
			}
			return func(form);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct changeTypes
	{
		static std::int64_t* thunk(RE::Actor* a_actor, std::int64_t* a1)
		{
			if (a_actor->IsPlayerRef() && (gripMode == ONEHANDEDGRIPMODE || gripMode == DUALWEILDGRIPMODE)) {
				auto rightHand = a_actor->GetEquippedObject(false);
				auto leftHand = a_actor->GetEquippedObject(true);

				if (rightHand && rightHand->IsWeapon()) {
					originalRightWeapon = rightHand->As<RE::TESObjectWEAP>()->GetWeaponType();
					if (mainFunctions::isTwoHanded(rightHand->As<RE::TESObjectWEAP>()))
							rightHand->As<RE::TESObjectWEAP>()->weaponData.animationType = RE::WEAPON_TYPE::kOneHandSword;
				}

				if (leftHand && leftHand->IsWeapon() && (rightHand && leftHand->GetFormID() != rightHand->GetFormID())) {
					originalLeftWeapon = leftHand->As<RE::TESObjectWEAP>()->GetWeaponType();
					if (mainFunctions::isTwoHanded(leftHand->As<RE::TESObjectWEAP>()))
							leftHand->As<RE::TESObjectWEAP>()->weaponData.animationType = RE::WEAPON_TYPE::kOneHandSword;
				}
			}
			return func(a_actor, a1);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct changeTypesBack
	{
		static std::int64_t* thunk(RE::Actor* a_actor, std::int64_t* a1)
		{
			if (a_actor->IsPlayerRef() && (gripMode == ONEHANDEDGRIPMODE || gripMode == DUALWEILDGRIPMODE)) 
			{
				auto rightHand = a_actor->GetEquippedObject(false);
				auto leftHand = a_actor->GetEquippedObject(true);

				if (rightHand && rightHand->IsWeapon()) {
					rightHand->As<RE::TESObjectWEAP>()->weaponData.animationType = originalRightWeapon;
				}

				if (leftHand && leftHand->IsWeapon() && rightHand && leftHand->GetFormID() != rightHand->GetFormID()) {
					leftHand->As<RE::TESObjectWEAP>()->weaponData.animationType = originalLeftWeapon;
				}
			}
			return func(a_actor, a1);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct getMeleeDamage
	{
		static float thunk(std::int64_t* a1, RE::ActorValueOwner* a2, float damageMult, bool isBow)
		{
			float d = func(a1, a2, damageMult, isBow);
			return d;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct dupeEquipSlot
	{
		static void setEquipSlot(RE::TESObjectWEAP* a_weap, bool back = false)
		{
			if (!mainFunctions::isTwoHanded(a_weap))  //discard bow/xbow
				return;

			if (back)
				a_weap->SetEquipSlot(twoHandSlot);
			else
				a_weap->SetEquipSlot(rightHandSlot);
		}

		static void convertAllWeaponSlots(RE::Actor* a_actor, RE::TESForm* a_form, bool back = false)
		{
			//change the slots of both weapons and the one that is to equip/unequip
			auto rH = a_actor->GetEquippedObject(false);
			auto lH = a_actor->GetEquippedObject(true);

			if (a_form && a_form->IsWeapon())
				setEquipSlot(a_form->As<RE::TESObjectWEAP>(), back);
			if (rH && rH->IsWeapon() && rH != a_form)
				setEquipSlot(rH->As<RE::TESObjectWEAP>(), back);
			if (lH && lH->IsWeapon() && lH != a_form && lH != rH)
				setEquipSlot(lH->As<RE::TESObjectWEAP>(), back);
		}

		static char thunk(std::int64_t* a1, RE::Actor* a_actor, std::int64_t* a3, RE::BGSEquipSlot* a4, char a5)
		{
			auto form = (RE::TESForm*)*a3;
			//dupe 2h equipslot for a 1h
			if (a_actor->IsPlayerRef() && mainFunctions::checkPerk(a_actor, true)) {
				
				convertAllWeaponSlots(a_actor, form);
				char result = func(a1, a_actor, a3, a4, a5);
				convertAllWeaponSlots(a_actor, form, true);
				return result;
			}

			return func(a1, a_actor,a3,a4,a5);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct EquipObject
	{
		static void thunk(std::int64_t* a1, RE::Actor* a_actor, RE::TESForm* form, std::int64_t* a4, int a5, std::int64_t* a6, char a7, char a8, char a9, char a10)
		{
			//dupe 2h equipslot for a 1h
			if (a_actor->IsPlayerRef() && mainFunctions::checkPerk(a_actor, true)) {
				dupeEquipSlot::convertAllWeaponSlots(a_actor, form);
				func(a1, a_actor, form, a4, a5, a6, a7, a8, a9, a10);
				dupeEquipSlot::convertAllWeaponSlots(a_actor, form, true);
				return;
			}
			return func(a1, a_actor, form, a4, a5, a6, a7, a8, a9, a10);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct UnEquipObject
	{
		static std::int64_t thunk(std::int64_t* a1, RE::Actor* a_actor, RE::TESForm* form, std::int64_t* a4)
		{
			//dupe 2h equipslot for a 1h
			if (a_actor->IsPlayerRef() && mainFunctions::checkPerk(a_actor, true)) {
				dupeEquipSlot::convertAllWeaponSlots(a_actor, form);
				std::int64_t a_result = func(a1, a_actor, form, a4);
				dupeEquipSlot::convertAllWeaponSlots(a_actor, form, true);
				return a_result;
			}
			return func(a1, a_actor, form, a4);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void install()
	{
		//getmeleedamage
		//REL::Relocation<std::uintptr_t> targetG{ RELOCATION_ID(0, 44001) };
		//stl::write_thunk_call<getMeleeDamage>(targetG.address() + REL ::Relocate(0x0, 0x1a4));

		//theres a bazillion weapon type verfications that prevent 2h weapons from being wielded as a 1hander
		//maybe its a behavior issue but duping the weapon type just works too
		//change weapon types
		REL::Relocation<std::uintptr_t> targetD{ RELOCATION_ID(41743, 42824) };
		stl::write_thunk_call<changeTypes>(targetD.address() + REL ::Relocate(0x22, 0x22));
		
		REL::Relocation<std::uintptr_t> targetE{ RELOCATION_ID(41743, 42824) };
		stl::write_thunk_call<changeTypesBack>(targetE.address() + REL ::Relocate(0xf8, 0xf8));
		
		
		REL::Relocation<std::uintptr_t> targetH{ RELOCATION_ID(37938, 38894) };  // - 66fa20	EquipObject
		stl::write_thunk_call<EquipObject>(targetH.address() + REL ::Relocate(0xe5, 0x170));

		REL::Relocation<std::uintptr_t> targetG{ RELOCATION_ID(37945, 38901) };  // - 670210	UnequipObject
		stl::write_thunk_call<UnEquipObject>(targetG.address() + REL ::Relocate(0x138, 0x1b9));

		//REL::Relocation<std::uintptr_t> targetG{ RELOCATION_ID(0, 38905) };  // - 670560	UnequipSpell
		//stl::write_thunk_call<dupeEquipSlotB>(targetG.address() + 0xc2);

		
		//dupeEquiptype hooks

		if (REL::Module::IsAE()) {
			constexpr std::array locationsA{
				std::make_pair<std::uint64_t, std::size_t>(40570, 0xf2),
				std::make_pair<std::uint64_t, std::size_t>(51149, 0x76),
				std::make_pair<std::uint64_t, std::size_t>(51543, 0x235),
				std::make_pair<std::uint64_t, std::size_t>(51548, 0xc2),  //8ba080
			};
			for (const auto& [id, offset] : locationsA) {
				REL::Relocation<std::uintptr_t> target(REL::ID(id), offset);
				stl::write_thunk_call<dupeEquipSlot>(target.address());
			}
		} else {
			constexpr std::array locationsA{
				std::make_pair<std::uint64_t, std::size_t>(39491, 0xf2),
				std::make_pair<std::uint64_t, std::size_t>(50649, 0x236),
				std::make_pair<std::uint64_t, std::size_t>(50654, 0xc4),
			};
			for (const auto& [id, offset] : locationsA) {
				REL::Relocation<std::uintptr_t> target(REL::ID(id), offset);
				stl::write_thunk_call<dupeEquipSlot>(target.address());
			}
		}

		//favorites-menu getequiptype icondisplay	
		REL::Relocation<std::uintptr_t> targetA{ RELOCATION_ID(50945, 51822) };  // - 8ccdd0
		stl::write_thunk_call<getEquipTypeIconDisplay>(targetA.address() + REL ::Relocate(0x9b, 0x94));
	}
}

void Init()
{
	loadIni();
	mainFunctions::Hook();
	controlMap::installHooks();
	Hooks::install();
	Events::Register();

	auto g_message = SKSE::GetMessagingInterface();
	g_message->RegisterListener([](SKSE::MessagingInterface::Message* msg) -> void 
	{
		if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
			RE::BSInputDeviceManager* inputEventDispatcher = RE::BSInputDeviceManager::GetSingleton();
			if (inputEventDispatcher) {
				auto dataHandler = RE::TESDataHandler::GetSingleton();
				rightHandSlot = dataHandler->LookupForm<RE::BGSEquipSlot>(0x13f42, "Skyrim.esm");
				twoHandSlot = dataHandler->LookupForm<RE::BGSEquipSlot>(0x13f45, "Skyrim.esm");
				leftHandSlot = dataHandler->LookupForm<RE::BGSEquipSlot>(0x13f43, "Skyrim.esm");
				shieldSlot = dataHandler->LookupForm<RE::BGSEquipSlot>(0x141e8, "Skyrim.esm");
				switchOut = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(0x3e62c, "Skyrim.esm");  //3c7c0
				switchIn = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(0x808, "DynamicGrip.esp");
				dgSpell = dataHandler->LookupForm<RE::SpellItem>(0x80F, "DynamicGrip.esp");


				if (strlen(reqPerkEditorID_1H) > 0)
					reqPerk1H = RE::TESForm::LookupByEditorID<RE::BGSPerk>(reqPerkEditorID_1H);
				if (strlen(reqPerkEditorID_2H) > 0)
					reqPerk2H = RE::TESForm::LookupByEditorID<RE::BGSPerk>(reqPerkEditorID_2H);
			}
		}

		if (msg->type == SKSE::MessagingInterface::kPostLoadGame || msg->type == SKSE::MessagingInterface::kNewGame)
		{
			RE::BSInputDeviceManager* inputEventDispatcher = RE::BSInputDeviceManager::GetSingleton();
			if (inputEventDispatcher) 
			{
				auto player = RE::PlayerCharacter::GetSingleton();
				if (dgSpell)
				{
					if (player->HasSpell(dgSpell))		//refresh spell in case theres new mgEffects to apply
						player->RemoveSpell(dgSpell);
					player->AddSpell(dgSpell);
				}


				player->GetGraphVariableInt("iDynamicGripMode", gripMode);
				isSwitching = false;
				isQueuedGripSwitch = false;
				previouLeftWeapon = nullptr;
			}
		}
	});
}

void InitializeLog()
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		util::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= fmt::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = spdlog::level::info;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(level);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%l] %v"s);
}

EXTERN_C [[maybe_unused]] __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
#ifndef NDEBUG
	while (!IsDebuggerPresent()) {};
#endif

	InitializeLog();

	logger::info("Loaded plugin");

	SKSE::Init(a_skse);

	Init();

	return true;
}

EXTERN_C [[maybe_unused]] __declspec(dllexport) constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary(true);
	v.HasNoStructUse();
	return v;
}();

EXTERN_C [[maybe_unused]] __declspec(dllexport) bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->name = SKSEPlugin_Version.pluginName;
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->version = SKSEPlugin_Version.pluginVersion;
	return true;
}

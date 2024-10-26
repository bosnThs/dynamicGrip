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

RE::MagicItem* lastBoundWeaponSpell;

RE::WEAPON_TYPE originalRightWeapon;
RE::WEAPON_TYPE originalLeftWeapon;

const int DEFAULTGRIPMODE = 0;
const int TWOHANDEDGRIPMODE = 1;
const int ONEHANDEDGRIPMODE = 2;
const int DUALWEILDGRIPMODE = 3;

std::uint16_t keyboardKey, keyboardMod, gamepadKey, gamepadMod;
char*         reqPerkEditorID_1H;
char*		  reqPerkEditorID_2H;
RE::BGSPerk*  reqPerk1H;
RE::BGSPerk*  reqPerk2H;

float fCombatDistance;
bool  bEnableNPC = true;
bool  bPlaySounds = true;

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
		
	bPlaySounds = (uint16_t)ini.GetBoolValue("settings", "bPlaySounds", true);
	bEnableNPC = (uint16_t)ini.GetBoolValue("settings", "bEnableNPC", false);
}

struct mainFunctions
{
	static void Hook()
	{
		//REL::Relocation<std::uintptr_t> SneakHandlerVtbl{ RE::VTABLE_SneakHandler[0] };
		//_CanProcessSneak = SneakHandlerVtbl.write_vfunc(0x4, CanProcessSneak);

		REL::Relocation<std::uintptr_t> ShoutHandlerVtbl{ RE::VTABLE_ShoutHandler[0] };
		_CanProcessShout = ShoutHandlerVtbl.write_vfunc(0x1, CanProcessShout);

		REL::Relocation<std::uintptr_t> PlayerCharacterVtbl{ RE::VTABLE_PlayerCharacter[0] };
		_OnItemEquipped = PlayerCharacterVtbl.write_vfunc(0xb2, OnItemEquipped);

		REL::Relocation<std::uintptr_t> StandardItemDataVtbl{ RE::VTABLE_StandardItemData[0] };
		_GetEquipState = StandardItemDataVtbl.write_vfunc(0x3, GetEquipState);

		
		if (bEnableNPC) 
		{
			//REL::Relocation<std::uintptr_t> CharacterVtbl{ RE::VTABLE_Character[2] };
			//_NotifyAnimationGraph = CharacterVtbl.write_vfunc(0x1, NotifyAnimationGraph);

			//REL::Relocation<std::uintptr_t> GetWeaponSlotVtbl{ RE::VTABLE_TESObjectWEAP[9] };
			//_GetWeaponSlot = GetWeaponSlotVtbl.write_vfunc(0x4, GetWeaponSlot);

			REL::Relocation<std::uintptr_t> HookTestVtbl{ RE::VTABLE_Character[0] };
			_UpdateCombat = HookTestVtbl.write_vfunc(0xe4, UpdateCombat);
		}
	}

	static bool CanProcessSneak(RE::SneakHandler* , RE::ButtonEvent* , RE::PlayerControlsData* )
	{
		//debugging
		auto player = RE::PlayerCharacter::GetSingleton();
		int  a = 1;
		player->SetGraphVariableInt("iLeftHandType", a);
		player->SetGraphVariableInt("iLeftHandEquipped", a);
		player->SetGraphVariableInt("iRightHandType", a);
		
		player->GetGraphVariableInt("iLeftHandType", a);
		player->GetGraphVariableInt("iRightHandType", a);

		return false;
		//return _CanProcessSneak(a_this, a_event, a_data);
	}
	static inline REL::Relocation<decltype(CanProcessSneak)> _CanProcessSneak;

	static RE::BGSEquipSlot* GetWeaponSlot(RE::BGSEquipType* a_this)
	{
		auto a_weaponAddr = reinterpret_cast<char*>(a_this) - 0xE0;  //BGSEquipType,               // 0E0
		RE::TESObjectWEAP* a_weapon = (RE::TESObjectWEAP*)a_weaponAddr;
		if (isTwoHanded(a_weapon)) {
			return rightHandSlot;
		}

		return a_this->equipSlot;
	}
	static inline REL::Relocation<decltype(GetWeaponSlot)> _GetWeaponSlot;

	static double getDistance(RE::NiPoint3 a, RE::NiPoint3 b)
	{
		return sqrt(pow(a.x - b.x, 2.0) + pow(a.y - b.y, 2.0) + pow(a.z - b.z, 2.0));
	}

	static float getAVPerc(RE::ActorPtr a_actor, RE::ActorValue a_value)
	{
		return a_actor->AsActorValueOwner()->GetActorValue(a_value) / a_actor->GetActorBase()->GetActorValue(a_value);
	}

	static bool getOffensiveStance(RE::CombatController* combatController)
	{
		auto state = combatController->state;
		auto actor = combatController->actorHandle.get();
		auto target = combatController->targetHandle.get();

		if (!target)
			return false;

		auto rightHand = actor->GetEquippedObject(false);
		auto leftHand = actor->GetEquippedObject(true);
		if (!rightHand || !rightHand->IsWeapon() || !isOneHanded(rightHand->As<RE::TESObjectWEAP>()))
			return false;
		if (leftHand && leftHand->IsWeapon() && isOneHanded(rightHand->As<RE::TESObjectWEAP>()))
			return false;

		int chance = 0;

		if (rightHand->As<RE::TESObjectWEAP>()->IsOneHandedDagger())
			chance--;

		if (!leftHand)
			chance++;

		if (getDistance(actor->GetPosition(), target->GetPosition()) < fCombatDistance * 1.5)
			chance++;

		bool tarStaggering;
		target->GetGraphVariableBool("isStaggering", tarStaggering);
		if (tarStaggering)
			chance++;

		if (getAVPerc(actor, RE::ActorValue::kStamina) < 0.40)
			chance--;

		if (getAVPerc(actor, RE::ActorValue::kHealth) < getAVPerc(target, RE::ActorValue::kHealth))
			chance--;

		if (getAVPerc(actor, RE::ActorValue::kHealth) > getAVPerc(target, RE::ActorValue::kHealth))
			chance++;

		if (state->confidenceModifier > 0.50)
			chance++;

		if (chance < 2)
			return false;
		return true;
	}

	static void UpdateCombat(RE::Character* a_this)
	{
		_UpdateCombat(a_this);
		auto combatController = a_this->GetActorRuntimeData().combatController;
		if (combatController) 
		{
			auto gripMode = getCurrentGripMode(a_this);
			switch (gripMode) 
			{
				case DEFAULTGRIPMODE:
					if (getOffensiveStance(combatController))
						gripSwitch(a_this);
					break;
				case TWOHANDEDGRIPMODE:
					if (!getOffensiveStance(combatController))
						gripSwitch(a_this);
					break;
				default:
					break;
			}
		}
	}
	static inline REL::Relocation<decltype(UpdateCombat)> _UpdateCombat;

	static void NotifyAnimationGraph(RE::Character* a_this, const RE::BSFixedString& a_eventName)
	{
		if (a_eventName == "changeGripMode")
			gripSwitch(a_this);

		return _NotifyAnimationGraph(a_this, a_eventName);
	}
	static inline REL::Relocation<decltype(NotifyAnimationGraph)> _NotifyAnimationGraph;

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

					int gripMode = mainFunctions::getCurrentGripMode(RE::PlayerCharacter::GetSingleton());
					if (gripMode == TWOHANDEDGRIPMODE)
						return 4;

					auto player = RE::PlayerCharacter::GetSingleton();
					auto rHand = player->GetEquippedObject(false);
					//auto rHandEntry = player->GetEquippedEntryData(false);
					auto lHand = player->GetEquippedObject(true);
					//auto lHandEntry = player->GetEquippedEntryData(true);

					if (a_result == 4 && rHand == lHand)// && rHandEntry == lHandEntry)
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

	static void setHandAnim(RE::Actor* a_actor, bool left, std::string animVar)
	{
		RE::BSFixedString s = "iLeftHand" + animVar;
		if (!left)
			s = "iRightHand" + animVar;
		a_actor->SetGraphVariableInt(s, getObjectType(a_actor->GetEquippedObject(left)));
	}

	static void setBothHandsAnim(RE::Actor* a_actor, std::string animVar = "Type")
	{
		setHandAnim(a_actor, false, animVar);
		setHandAnim(a_actor, true, animVar);
	}

	static void OnItemEquipped(RE::PlayerCharacter* a_this, bool anim)
	{
		int gripMode = getCurrentGripMode(a_this);
		if ((gripMode == ONEHANDEDGRIPMODE || gripMode == DUALWEILDGRIPMODE))  // && c== 1)
		{
			//set accurate values for iRightHandEquipped_DG/iLeftHandEquipped_DG Graphvars which hold the current drawn weapons type
			mainFunctions::setBothHandsAnim(a_this, "Equipped_DG");

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
		setBothHandsAnim(a_this);
	}
	static inline REL::Relocation<decltype(OnItemEquipped)> _OnItemEquipped;

	static bool CanProcessShout(RE::ShoutHandler* a_this, RE::InputEvent* a_event)
	{
		auto player = RE::PlayerCharacter::GetSingleton();

		auto s = a_event->QUserEvent();
		if (player->AsActorState()->IsWeaponDrawn() && s == "GripSwitch" && !a_event->AsButtonEvent()->IsPressed() || inputHandler(a_event)) {
			if (gripSwitch(player->As<RE::Actor>())) 
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

	static bool gripSwitch(RE::Actor* a_actor)
	{
		if (a_actor->AsActorState()->IsWeaponDrawn()) {
			RE::ATTACK_STATE_ENUM currentState = (a_actor->AsActorState()->actorState1.meleeAttackState);
			if (currentState != RE::ATTACK_STATE_ENUM::kNone && !isSwitching)
				return false;

			auto rightHand = a_actor->GetEquippedObject(false);
			auto leftHand = a_actor->GetEquippedObject(true);

			//only viable weapons to grip switch
			if (rightHand && rightHand->IsWeapon() && canSwitch(rightHand->As<RE::TESObjectWEAP>())) {
				auto rightWeapon = rightHand->As<RE::TESObjectWEAP>();

				isSwitching = true;
				int newGrip;
				switch (getCurrentGripMode(a_actor)) {
				case DEFAULTGRIPMODE:
					if (isOneHanded(rightWeapon)) {
						if (!checkPerk(a_actor, false))
							return false;
						//turn weapon in main hand into a 2h and remove left hand
						newGrip = TWOHANDEDGRIPMODE;

						//unequip left weapon
						checkAndUnequipLeft(a_actor, leftHand, true);

					} else {
						if (!checkPerk(a_actor, true))
							return false;

						//is twoHanded
						newGrip = ONEHANDEDGRIPMODE;
						//automatically equip a weapon in left hand
						checkAndEquipLeft(a_actor, previouLeftWeapon);
					}
					break;
				case TWOHANDEDGRIPMODE:
					//requip previous leftHand weapon
					newGrip = DEFAULTGRIPMODE;

					checkAndEquipLeft(a_actor, previouLeftWeapon);


					break;
				case ONEHANDEDGRIPMODE:
				case DUALWEILDGRIPMODE:
					//unequip left weapon
					checkAndUnequipLeft(a_actor, leftHand, true);

				default:
					newGrip = DEFAULTGRIPMODE;
					break;
				};

				a_actor->NotifyAnimationGraph("GripSwitchEvent");
				isSwitching = false;
				toggleGrip(a_actor, newGrip, true);
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

	static bool checkPerk(RE::Actor* a_actor, bool twoHand)
	{
		if (twoHand) 
		{
			if (!reqPerk2H || a_actor->HasPerk(reqPerk2H))
				return true;
		} else {
			if (!reqPerk1H || a_actor->HasPerk(reqPerk1H))
				return true;
		}
		return false;
	}

	static bool isOneHanded(RE::TESForm* a_weap)
	{
		if (!a_weap || !a_weap->IsWeapon())
			return false;

		auto weap = a_weap->As<RE::TESObjectWEAP>();
		if (weap->IsOneHandedSword() || weap->IsOneHandedDagger() || weap->IsOneHandedAxe() || weap->IsOneHandedMace())
			return true;
		return false;
	}

	static bool isTwoHanded(RE::TESForm* a_weap)
	{
		if (!a_weap || !a_weap->IsWeapon())
			return false;

		auto weap = a_weap->As<RE::TESObjectWEAP>();
		if (weap->IsTwoHandedSword() || weap->IsTwoHandedAxe())
			return true;
		return false;
	}

	static int getCurrentGripMode(RE::Actor* a_actor)
	{
		int a_result;
		a_actor->GetGraphVariableInt("iDynamicGripMode", a_result);
		return a_result;
	}

	static void toggleGrip(RE::Actor* a_actor, int mode, bool bPlaySound = false)
	{
		//gripMode = mode;

		//auto player = RE::PlayerCharacter::GetSingleton();
		a_actor->SetGraphVariableInt("iDynamicGripMode", mode);

		if (bPlaySound && bPlaySounds) 
		{
			if (mode == DEFAULTGRIPMODE)
				playSound(a_actor, switchOut);
			else
				playSound(a_actor, switchIn);
		}
	}

	static void playSound(RE::Actor* a_actor, RE::BGSSoundDescriptorForm* a_descriptor)
	{
		if (!a_descriptor || !a_actor->Is3DLoaded())
			return;

		RE::BSSoundHandle handle;
		handle.soundID = a_descriptor->GetFormID();
		handle.assumeSuccess = false;
		*(uint32_t*)&handle.state = 0;

		//handle.SetPosition(a_actor->GetPosition());

		handle.SetObjectToFollow(a_actor->Get3D());
		handle.SetVolume(1);

		auto sm = RE::BSAudioManager::GetSingleton();
		sm->BuildSoundDataFromDescriptor(handle, a_descriptor);
		sm->Play(a_descriptor);
	}

	static bool checkBoundWeapon(RE::TESForm* a_weapon)
	{
		if (!a_weapon->IsWeapon())
			return false;

		auto weapnFlags2 = a_weapon->As<RE::TESObjectWEAP>()->weaponData.flags2 & RE::TESObjectWEAP::Data::Flag2::kBoundWeapon;
		if (weapnFlags2)
			return true;

		return false;
	}

	static void checkAndEquipLeft(RE::Actor* a_actor, RE::TESForm* a_form)
	{
		if (!a_form)
			return;

		if (checkBoundWeapon(a_form))
			a_form = lastBoundWeaponSpell;

		if (a_actor->IsPlayerRef() && a_form)
			equipLeftSlot(a_actor, a_form);
	}

	static bool checkInventory(RE::Actor* a_actor, RE::TESForm* a_weapon)
	{
		//check if weapon/armor is still in inventory
		auto inventory = a_actor->GetInventory();
		for (auto& [item, data] : inventory) 
		{
			const auto& [count, entry] = data;
			if (count > 0 && item->GetFormID() == a_weapon->GetFormID()) 

				return true;
		}
		return false;
	}

	static void equipLeftSlot(RE::Actor* a_actor, RE::TESForm* a_weapon)
	{
		if (a_weapon->IsMagicItem() || checkInventory(a_actor, a_weapon)) 
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
					equipManager->EquipSpell(a_actor, a_weapon->As<RE::SpellItem>(), slot);
				else
					equipManager->EquipObject(a_actor, a_weapon->As<RE::TESBoundObject>(), a_weapon->As<RE::ExtraDataList>(), 1, slot, true, false, true, true);
			});
		}
	}

	static void checkAndUnequipLeft(RE::Actor* a_actor, RE::TESForm* a_form, bool leftH)
	{
		if (a_actor->IsPlayerRef())
			previouLeftWeapon = a_form;  //save previous left weapon/shield/spell even if empty if player

		if (a_form)
			unequipLeftSlot(a_actor, leftH);
	}

	static void unequipLeftSlot(RE::Actor* a_actor, bool leftH, bool now = false)
	{
		auto form = a_actor->GetEquippedObject(leftH);
		if (!form)
			return;

		RE::BGSEquipSlot* slot;
		if (form->IsArmor())  //shield check
			slot = shieldSlot;
		else
			slot = leftHandSlot;

		if (now) 
			unequipSlotNOW(a_actor, slot);
		 else 
			unequipSlot(a_actor, slot);
	}

	static void unequipSlotNOW(RE::Actor* a_actor, RE::BGSEquipSlot* slot)
	{
		auto* form = RE::TESForm::LookupByID<RE::TESForm>(0x00020163);  //dummydagger
		if (!form) {
			return;
		}
		auto* proxy = form->As<RE::TESObjectWEAP>();
		auto* equip_manager = RE::ActorEquipManager::GetSingleton();
		equip_manager->EquipObject(a_actor, proxy, nullptr, 1, slot, false, true, false);
		equip_manager->UnequipObject(a_actor, proxy, nullptr, 1, slot, false, true, false);
		return;
	}

	static void unequipSlot(RE::Actor* a_actor, RE::BGSEquipSlot* slot)
	{
		auto* task = SKSE::GetTaskInterface();
		if (task) {
			auto* form = RE::TESForm::LookupByID<RE::TESForm>(0x00020163); //dummydagger
			if (!form) {
				return;
			}
			auto* proxy = form->As<RE::TESObjectWEAP>();
			auto* equip_manager = RE::ActorEquipManager::GetSingleton();
			task->AddTask([=]() { equip_manager->EquipObject(a_actor, proxy, nullptr, 1, slot, false, true, false); });
			task->AddTask([=]() { equip_manager->UnequipObject(a_actor, proxy, nullptr, 1, slot, false, true, false); });
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

		static bool checkBoundWeaponSpell(RE::MagicItem* a_spell)
		{
			for (auto effect : a_spell->effects)
			{
				if (effect->baseEffect->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kBoundWeapon)
					return true;
			}
			return false;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>* ) override
		{
			if (!a_event || !a_event->actor)// || !a_event->actor->IsPlayerRef())
				return RE::BSEventNotifyControl::kContinue;

			auto ui = RE::UI::GetSingleton();
			if (ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) 
			{
				auto invMenu = ui->GetMenu<RE::InventoryMenu>(RE::InventoryMenu::MENU_NAME);
				invMenu.get()->GetRuntimeData().itemList->Update();
			}

			//auto* a_actor = RE::PlayerCharacter::GetSingleton();
			auto  a_actor = a_event->actor->As<RE::Actor>();

			auto rightHand = a_actor->GetEquippedObject(false);
			auto leftHand = a_actor->GetEquippedObject(true);

			auto* form = RE::TESForm::LookupByID(a_event->baseObject);
			int   gripMode = mainFunctions::getCurrentGripMode(a_actor);

			if (a_event->equipped) 
			{
				//remove cached left-weapon to prevent duping
				if (a_actor->IsPlayerRef() && previouLeftWeapon && previouLeftWeapon->GetFormID() == form->GetFormID())  
					previouLeftWeapon = nullptr;

				//unequip left hand if equipping a 2hander in right hand
				if (rightHand && rightHand->IsWeapon() && rightHand->GetFormID() == form->GetFormID() && mainFunctions::isTwoHanded(rightHand->As<RE::TESObjectWEAP>()) && rightHand != leftHand ) 
				{
					mainFunctions::unequipLeftSlot(a_actor, true, true);
					return RE::BSEventNotifyControl::kContinue;
				}

				if (leftHand && leftHand->GetFormID() == form->GetFormID() || rightHand && rightHand->GetFormID() == form->GetFormID()) 
				{
					switch (gripMode) {
						case TWOHANDEDGRIPMODE:  //main-hand base is 1H
						mainFunctions::toggleGrip(a_actor, DEFAULTGRIPMODE);
							break;

						//case DEFAULTGRIPMODE:
						//case ONEHANDEDGRIPMODE:
						//case DUALWEILDGRIPMODE:
						default:
							//no 2h grip-switch for npcs
							if (!a_actor->IsPlayerRef() || !mainFunctions::checkPerk(a_actor, true))
								return RE::BSEventNotifyControl::kContinue;

							//equipped 2h in right hand
							if (rightHand && rightHand->IsWeapon() && mainFunctions::isTwoHanded(rightHand->As<RE::TESObjectWEAP>()) ) 
							{
								if (leftHand) 
								{
									int newGrip = ONEHANDEDGRIPMODE;

									if (leftHand->IsWeapon() && !leftHand->As<RE::TESObjectWEAP>()->IsStaff())
										newGrip = DUALWEILDGRIPMODE;

									if (gripMode == DEFAULTGRIPMODE)
										a_actor->NotifyAnimationGraph("GripSwitchEvent");

									mainFunctions::toggleGrip(a_actor, newGrip);
									//a_actor->OnItemEquipped(false);
								}
								return RE::BSEventNotifyControl::kContinue;
							}
							break;
					};
				}
			} else {
				if (form->GetFormType() == RE::FormType::Enchantment || mainFunctions::checkBoundWeapon(form))
					return RE::BSEventNotifyControl::kContinue;

				//unequipped bound spell on left hand
				if (a_actor->IsPlayerRef() && !leftHand && form->IsMagicItem() && checkBoundWeaponSpell(form->As<RE::MagicItem>())) 
				{
					lastBoundWeaponSpell = form->As<RE::MagicItem>();
					return RE::BSEventNotifyControl::kContinue;
				}

				//reset grip
				if ((!rightHand || !leftHand) && (gripMode == ONEHANDEDGRIPMODE || gripMode == DUALWEILDGRIPMODE))
				{
					a_actor->NotifyAnimationGraph("GripSwitchEvent");
					mainFunctions::toggleGrip(a_actor, DEFAULTGRIPMODE);

					//bandaid for a scroll-related bug
					if (form->GetFormType() == RE::FormType::Scroll || form->GetFormType() == RE::FormType::Spell)	
						mainFunctions::setBothHandsAnim(a_actor);
				}

				//reset grip if no weapons
				if ((!rightHand && !leftHand) && gripMode != DEFAULTGRIPMODE)
				{
					a_actor->NotifyAnimationGraph("GripSwitchEvent");
					mainFunctions::toggleGrip(a_actor, DEFAULTGRIPMODE);
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
			if (form && form->IsWeapon()) 
			{
				int gripMode = mainFunctions::getCurrentGripMode(RE::PlayerCharacter::GetSingleton());
				if (mainFunctions::isTwoHanded(form->As<RE::TESObjectWEAP>())) {
					if (gripMode == ONEHANDEDGRIPMODE || gripMode == DUALWEILDGRIPMODE)
						return (std::int64_t)rightHandType;
					else
						return (std::int64_t)twoHandType;
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
		static void changeTypesFunction(RE::Actor* a_actor)
		{
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

		static std::int64_t* thunk(RE::Actor* a_actor, std::int64_t* a1)
		{
			int gripMode = mainFunctions::getCurrentGripMode(a_actor);
			if (a_actor->IsPlayerRef() && (gripMode == ONEHANDEDGRIPMODE || gripMode == DUALWEILDGRIPMODE)) {

				changeTypesFunction(a_actor);
				
			}

			return func(a_actor, a1);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct changeTypesBack
	{
		static void changeTypesBackFunction(RE::Actor* a_actor)
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

		static std::int64_t* thunk(RE::Actor* a_actor, std::int64_t* a1)
		{
			int gripMode = mainFunctions::getCurrentGripMode(a_actor);
			if (a_actor->IsPlayerRef() && (gripMode == ONEHANDEDGRIPMODE || gripMode == DUALWEILDGRIPMODE)) {
				changeTypesBackFunction(a_actor);

			}

			return func(a_actor, a1);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct dupeEquipSlot
	{
		static bool twoHWeaponEquipChecks(RE::Actor* a_actor)
		{
			if (a_actor->IsPlayerRef() && mainFunctions::checkPerk(a_actor, true))
				return true;
			return false;
		}

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

		static char thunk(std::int64_t* a1, RE::Actor* a_actor, std::int64_t* a3, RE::BGSEquipSlot* a_equipSlot, char a5)
		{
			auto form = (RE::TESForm*)*a3;
			//dupe 2h equipslot for a 1h
			if (twoHWeaponEquipChecks(a_actor)) 
			{				
				//prevent 2h in left-hand if main hander isnt a 2h
				if (mainFunctions::isTwoHanded(form) && a_equipSlot == leftHandSlot && !mainFunctions::isTwoHanded(a_actor->GetEquippedObject(false)))
					a_equipSlot = rightHandSlot;

				convertAllWeaponSlots(a_actor, form);
				char result = func(a1, a_actor, a3, a_equipSlot, a5);
				convertAllWeaponSlots(a_actor, form, true);
				return result;
			}

			return func(a1, a_actor, a3, a_equipSlot, a5);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct EquipObject
	{
		static void thunk(std::int64_t* a1, RE::Actor* a_actor, RE::TESForm* a_form, std::int64_t* a4, int a5, std::int64_t* a_equipSlot, char a7, char a8, char a9, char a10)
		{
			//dupe 2h equipslot for a 1h
			if (dupeEquipSlot::twoHWeaponEquipChecks(a_actor)) 
			{
				dupeEquipSlot::convertAllWeaponSlots(a_actor, a_form);
				func(a1, a_actor, a_form, a4, a5, a_equipSlot, a7, a8, a9, a10);
				dupeEquipSlot::convertAllWeaponSlots(a_actor, a_form, true);
				return;
			}
			return func(a1, a_actor, a_form, a4, a5, a_equipSlot, a7, a8, a9, a10);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct UnEquipObject
	{
		static std::int64_t thunk(std::int64_t* a1, RE::Actor* a_actor, RE::TESForm* form, std::int64_t* a4)
		{
			//dupe 2h equipslot for a 1h
			if (dupeEquipSlot::twoHWeaponEquipChecks(a_actor)) 
			{
				dupeEquipSlot::convertAllWeaponSlots(a_actor, form);
				std::int64_t a_result = func(a1, a_actor, form, a4);
				dupeEquipSlot::convertAllWeaponSlots(a_actor, form, true);
				return a_result;
			}
			return func(a1, a_actor, form, a4);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BoundWeaponEquipObject
	{
		static void thunk(std::int64_t* a1, RE::Actor* a_actor, RE::TESForm* form, std::int64_t* a_spells, RE::BGSEquipSlot* a_equipSlot)
		{

			//dupe 2h equipslot for a 1h
			if (a_actor->IsPlayerRef() && mainFunctions::checkPerk(a_actor, true)) 
			{
				auto effectAddr = reinterpret_cast<char*>(a_spells) - 0x98;  //BSTArray<SpellItem*> spells;  // 98
				RE::ActiveEffect* a_activeEffect = (RE::ActiveEffect*)effectAddr;
				if (a_activeEffect->castingSource == RE::MagicSystem::CastingSource::kLeftHand && mainFunctions::isTwoHanded(a_actor->GetEquippedObject(false)))
					a_equipSlot = leftHandSlot;

				dupeEquipSlot::convertAllWeaponSlots(a_actor, form);
				func(a1, a_actor, form, a_spells, a_equipSlot);
				dupeEquipSlot::convertAllWeaponSlots(a_actor, form, true);
				return;
			}
			return func(a1, a_actor, form, a_spells, a_equipSlot);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BlockNPCEquip
	{
		static void thunk(std::int64_t* a1, RE::Actor* a_actor, RE::TESForm* a_form, std::int64_t* a4, int a5, std::int64_t* a6, char a7, char a8, char a9, char a10)
		{
			if (mainFunctions::getCurrentGripMode(a_actor) == DEFAULTGRIPMODE)
				return func(a1, a_actor, a_form, a4, a5, a6, a7, a8, a9, a10);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
	
	static void install()
	{
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

		REL::Relocation<std::uintptr_t> targetF{ RELOCATION_ID(33455, 34229) };  // 545F80 - 66FD20	boundweaponequip
		stl::write_thunk_call<BoundWeaponEquipObject>(targetF.address() + REL ::Relocate(0xba, 0xde));

		
		//dupeEquiptype hooks

		if (REL::Module::IsAE()) {
			constexpr std::array locationsA{
				std::make_pair<std::uint64_t, std::size_t>(40570, 0xf2),
				std::make_pair<std::uint64_t, std::size_t>(51149, 0x76),
				std::make_pair<std::uint64_t, std::size_t>(51543, 0x235),
				std::make_pair<std::uint64_t, std::size_t>(51548, 0xc2),  //8ba080
				std::make_pair<std::uint64_t, std::size_t>(51870, 0x85),  //8ba080
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
				std::make_pair<std::uint64_t, std::size_t>(50991, 0x8f),
			};
			for (const auto& [id, offset] : locationsA) {
				REL::Relocation<std::uintptr_t> target(REL::ID(id), offset);
				stl::write_thunk_call<dupeEquipSlot>(target.address());
			}
		}

		//favorites-menu getequiptype icondisplay	
		REL::Relocation<std::uintptr_t> targetA{ RELOCATION_ID(50945, 51822) };  // - 8ccdd0
		stl::write_thunk_call<getEquipTypeIconDisplay>(targetA.address() + REL ::Relocate(0x9b, 0x94));

		
		if (bEnableNPC) 
		{
			//NPC Equip hook
			REL::Relocation<std::uintptr_t> targetI{ RELOCATION_ID(46955, 48124) };
			stl::write_thunk_call<BlockNPCEquip>(targetI.address() + REL ::Relocate(0x1a5, 0x1d6));
		}
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
				switchOut = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(0x810, "DynamicGrip.esp");  //3c7c0
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

				auto gameSettings = RE::GameSettingCollection::GetSingleton();
				fCombatDistance = gameSettings->GetSetting("fCombatDistance")->GetFloat();

				//player->GetGraphVariableInt("iDynamicGripMode", gripMode);
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

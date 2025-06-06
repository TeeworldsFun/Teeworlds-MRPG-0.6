#include "mob_ai.h"

#include <game/server/entity_manager.h>
#include <game/server/entities/character_bot.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/gamecontext.h>

CMobAI::CMobAI(MobBotInfo* pNpcInfo, CPlayerBot* pPlayer, CCharacterBotAI* pCharacter)
	: CBaseAI(pPlayer, pCharacter), m_pMobInfo(pNpcInfo) { }

bool CMobAI::CanDamage(CPlayer* pFrom)
{
	if(!pFrom->IsBot())
		return true;

	const auto* pFromBot = static_cast<CPlayerBot*>(pFrom);
	if(pFromBot && (pFromBot->GetBotType() == TYPE_BOT_EIDOLON || pFromBot->GetBotType() == TYPE_BOT_NPC))
		return true;

	return false;
}

void CMobAI::OnSpawn()
{
	m_EmotionStyle = EMOTE_ANGRY;

	if(m_pMobInfo->m_Boss)
	{
		EnableBotIndicator(POWERUP_WEAPON, WEAPON_HAMMER);
		GS()->ChatWorld(m_pMobInfo->m_WorldID, nullptr, "In your zone emerging {}!", m_pMobInfo->GetName());
		GS()->CreatePlayerSound(-1, SOUND_BOSS_RESPAWN);
	}
}

void CMobAI::OnGiveRandomEffect(int ClientID)
{
	CPlayer* pPlayer = GS()->GetPlayer(ClientID);
	if(!pPlayer)
		return;

	if(const auto* pBuff = m_pMobInfo->GetRandomDebuff())
	{
		pPlayer->m_Effects.Add(pBuff->getEffect().c_str(), pBuff->getTime(), pBuff->getChance());
	}
}

void CMobAI::OnHandleTunning(CTuningParams* pTuning)
{
	// behavior slower
	if(m_pMobInfo->HasBehaviorFlag(MOBFLAG_BEHAVIOR_SLOWER))
	{
		pTuning->m_Gravity = 0.25f;
		pTuning->m_GroundJumpImpulse = 8.0f;
		pTuning->m_AirFriction = 0.75f;
		pTuning->m_AirControlAccel = 1.0f;
		pTuning->m_AirControlSpeed = 3.75f;
		pTuning->m_AirJumpImpulse = 8.0f;
		pTuning->m_HookFireSpeed = 30.0f;
		pTuning->m_HookDragAccel = 1.5f;
		pTuning->m_HookDragSpeed = 8.0f;
		pTuning->m_PlayerHooking = 0;
	}
}

void CMobAI::OnRewardPlayer(CPlayer* pPlayer, vec2 Force) const
{
	const int ClientID = pPlayer->GetCID();
	const int MobLevel = m_pMobInfo->m_Level;
	const int PlayerLevel = pPlayer->Account()->GetLevel();
	const bool showMessages = pPlayer->GetItem(itShowDetailGainMessages)->IsEquipped();

	// grinding gold
	{
		if(pPlayer->Account()->GetGold() < pPlayer->Account()->GetGoldCapacity())
		{
			const int goldGain = calculate_gold_gain(g_Config.m_SvMobKillGoldFactor, PlayerLevel, MobLevel, true);

			if(showMessages)
			{
				GS()->Chat(ClientID, "You gained {} gold.", goldGain);
			}

			pPlayer->Account()->AddGold(goldGain, true);
		}
	}

	// grinding experience
	{
		int expGain = calculate_exp_gain(g_Config.m_SvMobKillExpFactor, PlayerLevel, MobLevel);
		const int expBonusDrop = maximum(expGain / 3, 1);

		GS()->ApplyExperienceMultiplier(&expGain);

		if(showMessages)
		{
			GS()->Chat(ClientID, "You gained {} exp.", expGain);
		}

		GS()->EntityManager()->ExpFlyingPoint(m_pCharacter->m_Core.m_Pos, ClientID, expGain, Force);
		GS()->EntityManager()->DropPickup(m_pCharacter->m_Core.m_Pos, POWERUP_ARMOR, 0, expBonusDrop, (1 + rand() % 2), Force);
	}

	// drop item's
	{
		const auto ActiveLuckyDrop = pPlayer->GetAttributeChance(AttributeIdentifier::LuckyDropItem).value_or(0.f);

		for(int i = 0; i < MAX_DROPPED_FROM_MOBS; i++)
		{
			const int DropID = m_pMobInfo->m_aDropItem[i];
			const int DropValue = m_pMobInfo->m_aValueItem[i];

			if(DropID <= 0 || DropValue <= 0)
				continue;

			const float RandomDrop = clamp(m_pMobInfo->m_aRandomItem[i] + ActiveLuckyDrop, 0.0f, 100.0f);
			const vec2 ForceRandom = random_range_pos(Force, 4.f);

			CItem DropItem;
			DropItem.SetID(DropID);
			DropItem.SetValue(DropValue);
			GS()->EntityManager()->RandomDropItem(m_pCharacter->m_Core.m_Pos, ClientID, RandomDrop, DropItem, ForceRandom);
		}
	}

	// skill points
	{
		float BaseChance = m_pMobInfo->m_Boss ? g_Config.m_SvSkillPointsDropChanceRareMob : g_Config.m_SvSkillPointsDropChanceMob;
		const int LevelDifference = PlayerLevel - MobLevel;

		// check leveling difference
		if(LevelDifference > 0)
		{
			const int AdjustmentSteps = LevelDifference / 5;
			BaseChance -= BaseChance * (0.10f * AdjustmentSteps);
		}
		BaseChance = maximum(1.0f, BaseChance);

		// try generate chance
		if(random_float(100.f) <= BaseChance)
		{
			CPlayerItem* pPlayerItem = pPlayer->GetItem(itSkillPoint);
			pPlayerItem->Add(1);
			GS()->Chat(ClientID, "Skill points increased. Now you have '{} SP'!", pPlayerItem->GetValue());
		}
	}

	// append defeat mob progress
	GS()->Core()->QuestManager()->TryAppendDefeatProgress(pPlayer, m_pMobInfo->m_BotID);
}

void CMobAI::OnTargetRules(float Radius)
{
	const auto* pTarget = GS()->GetPlayer(m_Target.GetCID(), false, true);
	auto* pPlayer = SearchPlayerCondition(Radius, [&](const CPlayer* pCandidate)
	{
		const bool DamageDisabled = pCandidate->GetCharacter()->m_Core.m_DamageDisabled;

		if(pTarget)
		{
			const int CurrentTotalAttHP = pTarget->GetTotalAttributeValue(AttributeIdentifier::HP);
			const int CandidateTotalAttHP = pCandidate->GetTotalAttributeValue(AttributeIdentifier::HP);
			return !DamageDisabled && (CurrentTotalAttHP < CandidateTotalAttHP);
		}

		const bool AgressionFactor = GS()->IsWorldType(WorldType::Dungeon) || rand() % 30 == 0;
		return !DamageDisabled && AgressionFactor;
	});

	if(!pPlayer)
	{
		pPlayer = SearchPlayerBotCondition(Radius, [&](CPlayerBot* pCandidate)
		{
			return m_pCharacter->IsAllowedPVP(pCandidate->GetCID());
		});
	}

	if(pPlayer)
	{
		m_Target.Set(pPlayer->GetCID(), 100);
	}
}

void CMobAI::Process()
{
	// behavior sleepy
	if(m_pMobInfo->HasBehaviorFlag(MOBFLAG_BEHAVIOR_SLEEPY) &&
		(m_pPlayer->m_aPlayerTick[LastDamage] + (Server()->TickSpeed() * 5)) < Server()->Tick())
	{
		if(Server()->Tick() % Server()->TickSpeed() == 0)
		{
			GS()->SendEmoticon(m_ClientID, EMOTICON_ZZZ);
			m_pCharacter->SetEmote(EMOTE_BLINK, 2, false);
		}
		m_pCharacter->ResetInput();
		return;
	}

	// update
	m_pCharacter->UpdateTarget(1000.f);
	m_pCharacter->ResetInput();

	if(const auto* pTargetChar = GS()->GetPlayerChar(m_Target.GetCID()))
	{
		m_pPlayer->m_TargetPos = pTargetChar->GetPos();
		m_pCharacter->Fire();
	}
	else
	{
		m_pPlayer->m_TargetPos.reset();
	}

	m_pCharacter->SelectWeaponAtRandomInterval();
	m_pCharacter->Move();

	if(m_pMobInfo->m_Boss)
	{
		ShowHealth();
	}
}

void CMobAI::ShowHealth() const
{
	const int BotID = m_pPlayer->GetBotID();
	const int Health = m_pPlayer->GetHealth();
	const int StartHealth = m_pPlayer->GetMaxHealth();
	const float Percent = translate_to_percent((float)StartHealth, (float)Health);
	std::string ProgressBar = mystd::string::progressBar(100, (int)Percent, 10, "\u25B0", "\u25B1");

	for(const auto& ClientID : m_pCharacter->GetListDmgPlayers())
	{
		if(GS()->GetPlayer(ClientID, true))
		{
			GS()->Broadcast(ClientID, BroadcastPriority::GamePriority, 100, "{} {}({}/{})",
				DataBotInfo::ms_aDataBot[BotID].m_aNameBot, ProgressBar.c_str(), Health, StartHealth);
		}
	}
}
#include "GameActions.h"
#include "Context.h"
#include "Globals.h"
#include "Utils.h"
#include <algorithm>
#include <cstdio>
#include <core/Functions.h>
#include <kenshi/Bounty.h>
#include <kenshi/BountyManager.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Character.h>
#include <kenshi/Enums.h>
#include <kenshi/StateBroadcastData.h>
#include <kenshi/Dialogue.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/Platoon.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/RootObjectFactory.h>
#include <kenshi/SensoryData.h>
#include <kenshi/Town.h>
#include <kenshi/util/YesNoMaybe.h>
#include <kenshi/util/hand.h>
#include <ogre/OgreColourValue.h>
#include <vector>

// Forward-declared access to the chat player name stored by the UI layer
namespace SentientSands {
namespace UI {
extern std::string g_chatPlayerNameStr;
}
} // namespace SentientSands

void PerformLeaveSquad(Character *npc, GameWorld *world,
                       const std::string &originFaction) {
  if (!npc || !world)
    return;

  std::string factionPart = originFaction;
  std::string platoonPart = "";
  size_t pipePos = originFaction.find('|');
  if (pipePos != std::string::npos) {
    factionPart = originFaction.substr(0, pipePos);
    platoonPart = originFaction.substr(pipePos + 1);
  }

  Log("ACTION_EXEC: Dismissing " + npc->getName() + " (Target Faction: " +
      factionPart + ", Target Platoon: " + platoonPart + ")");

  if (world->player) {
    world->player->unselectPlayerCharacter(npc);
    lektor<Character *> &pc = world->player->playerCharacters;
    for (uint32_t i = 0; i < pc.size(); ++i) {
      if (pc.stuff[i] == npc) {
        for (uint32_t j = i; j < pc.size() - 1; ++j)
          pc.stuff[j] = pc.stuff[j + 1];
        pc.count--;
        Log("ACTION_EXEC: Removed " + npc->getName() +
            " from playerCharacters list.");
        break;
      }
    }
  }

  if (world->factionMgr) {
    FactionManager *fm = world->factionMgr;

    std::string targetFactionName = "Drifters";
    if (!factionPart.empty() && factionPart != "Unknown") {
      targetFactionName = factionPart;
    } else if (g_originFactions.count(npc->getHandle().serial)) {
      targetFactionName = g_originFactions[npc->getHandle().serial];
    }

    Faction *targetFaction = fm->getFactionByName(targetFactionName);

    // If Drifters requested or origin is missing, try to find the character's
    // original faction but exclude the player faction.
    if ((factionPart.empty() || factionPart == "Unknown" ||
         targetFactionName == "Drifters") &&
        npc->getGameData()) {
      GameData *characterData = npc->getGameData();
      // Try to find the original faction link in the character's template data
      const Ogre::vector<GameDataReference>::type *refs =
          characterData->getReferenceListIfExists("faction");
      if (refs && !refs->empty()) {
        Faction *refFaction = fm->getFactionByStringID(refs->at(0).sid);
        if (refFaction && !refFaction->isThePlayer()) {
          targetFaction = refFaction;
          targetFactionName = targetFaction->getName();
        }
      }
    }

    // Give fallback if origin doesn't exist (e.g. invalid string)
    if (!targetFaction || targetFaction->isThePlayer() ||
        targetFaction->isNotARealFaction()) {
      targetFactionName = "Drifters";
      targetFaction = fm->getFactionByName("Drifters");
    }

    if (!targetFaction || targetFaction->isThePlayer()) {
      targetFaction = NULL;
      const lektor<Faction *> *all = fm->getAllFactions();
      if (all) {
        for (uint32_t i = 0; i < all->count; ++i) {
          Faction *f = all->stuff[i];
          if (f && !f->isThePlayer() && !f->isNotARealFaction()) {
            targetFaction = f;
            if (f->getName() == targetFactionName)
              break;
          }
        }
      }
    }

    if (targetFaction) {
      Log("ACTION_EXEC: Moving character to target faction: " +
          targetFaction->getName());

      ActivePlatoon *ap = NULL;

      // Attempt to find existing platoon if requested
      if (!platoonPart.empty()) {
        const lektor<Platoon *> *activePlats =
            targetFaction->getActivePlatoons();
        if (activePlats) {
          for (uint32_t i = 0; i < activePlats->count; ++i) {
            Platoon *p = activePlats->stuff[i];
            if (p && (p->stringID == platoonPart ||
                      p->getPlatoonStringID() == platoonPart)) {
              ap = p->getActivePlatoon();
              if (ap) {
                Log("ACTION_EXEC: Found existing active platoon: " +
                    platoonPart);
                break;
              }
            }
          }
        }
      }

      // Fallback: Create a new platoon if no existing one found/active
      if (!ap) {
        Platoon *newPlat = targetFaction->createNewEmptyActivePlatoon(
            NULL, true, npc->getPosition());
        if (newPlat) {
          ap = newPlat->getActivePlatoon();
          Log("ACTION_EXEC: Created new platoon for dismissal.");
        }
      }

      if (ap) {
        npc->setFaction(targetFaction, ap);

        // Ensure the platoon has a leader if it was just created
        if (ap->getSquadSize() == 1 || !ap->getSquadLeader()) {
          ap->setSquadLeader(npc);
        }

        // --- RESTORE NPC DATA PACKAGES ---
        // Restore standard NPC AI systems (was using Player AI)
        npc->setupAI();
        npc->setupPlatoonAI();

        // Stabilize home town if currently in one (recruits often lose this)
        TownBase *currentTown = npc->getCurrentTownLocation();
        if (currentTown) {
          Ownerships *own = npc->getOwnerships();
          if (own)
            own->setHomeTown(currentTown, SQ_RESIDENT); // Use RESIDENT in town
        }

        npc->reThinkCurrentAIAction();
      } else {
        Log("ACTION_EXEC: ERROR: Could not create or find a platoon for "
            "dismissal!");
      }
    }
  }
}

// Helper to convert internal TaskType enums to human-readable strings for
// UI/Logging
std::string GetTaskName(TaskType tt) {
  switch ((int)tt) {
  case 1:
    return "MOVE_ON_FREE_WILL";
  case 4:
    return "MELEE_ATTACK";
  case 14:
    return "IDLE";
  case 15:
    return "WANDER_TOWN";
  case 18:
    return "RAID_TOWN";
  case 19:
    return "GO_HOMEBUILDING";
  case 20:
    return "STAND_AT_SHOPKEEPER_NODE";
  case 23:
    return "ATTACK_TOWN";
  case 24:
    return "WANDERER";
  case 35:
    return "RUN_AWAY";
  case 36:
    return "PATROL_TOWN";
  case 44:
    return "FOLLOW_PLAYER_ORDER";
  case 46:
    return "CHASE";
  case 53:
    return "TRAVEL_TO_TARGET_TOWN";
  case 55:
    return "BODYGUARD";
  case 57:
    return "JOB_REPAIR_ROBOT";
  case 58:
    return "JOB_MEDIC";
  case 67:
    return "MOVE_ON_FREE_WILL_FAST";
  case 105:
    return "FIND_AND_RESCUE";
  case 110:
    return "RELEASE_PRISONER";
  case 111:
    return "BREAKOUT_PRISONER";
  case 185:
    return "CUT_SHACKLES";
  case 201:
    return "PICK_LOCK_ON_SHACKLES";
  default:
    return "TASK_" + ToString((int)tt);
  }
}

// Сдавшиеся под присмотром. Движок сбрасывает нейтралитет через несколько
// секунд, и боец, отбежав, возвращается в драку. Поэтому какое-то время
// после сдачи мы раз за разом возвращаем его в мирное состояние.
static std::vector<std::pair<int, Character *> > g_surrenderWatch;

void ExecuteQueuedActions(GameWorld *thisptr, int &inventoryTimer) {
  // ── Удержание сдавшихся в мирном состоянии ───────────────────────────────
  if (!g_surrenderWatch.empty()) {
    for (size_t wi = 0; wi < g_surrenderWatch.size();) {
      g_surrenderWatch[wi].first--;
      if (g_surrenderWatch[wi].first > 0) {
        // УДЕРЖАНИЕ. Движок сбрасывает нейтралитет через несколько секунд,
        // и сдавшийся, отбежав, возвращается в драку. Поэтому пока идёт
        // наблюдение, раз за разом возвращаем его в мирное состояние.
        Character *hold = g_surrenderWatch[wi].second;
        try {
          if (hold && (uintptr_t)hold > 0x1000) {
            StateBroadcastData *hsb = hold->getStateBroadcast();
            if (hsb && (uintptr_t)hsb > 0x1000) {
              hsb->isTemporaryNeutral = true;
              hsb->aggressionTowardsTarget = 0.0f;
              hsb->intendedAggression = 0.0f;
            }
            // Если всё же ввязался в драку снова — выводим опять.
            if (hold->isInCombatMode(true, true)) {
              hold->endCombatMode();
              hold->sheatheWeapon();
            }
          }
        } catch (...) {
        }
        ++wi;
        continue;
      }
      // Срок удержания вышел — снимаем с наблюдения.
      g_surrenderWatch.erase(g_surrenderWatch.begin() + wi);
    }
  }

  std::deque<QueuedAction> localQueue;
  if (TryEnterCriticalSection(&g_uiMutex)) {
    localQueue = g_uiActionQueue;
    g_uiActionQueue.clear();
    LeaveCriticalSection(&g_uiMutex);
  }

  bool transactionFailed = false;
  std::string failureReason = "";

  for (size_t actIdx = 0; actIdx < localQueue.size(); ++actIdx) {
    try {
      const QueuedAction &act = localQueue[actIdx];
      Character *npc = act.actor.getCharacter();
      Character *target = act.target.getCharacter();

      if (act.type == ACT_NOTIFY) {
        thisptr->showPlayerAMessage_withLog(act.message, true);
      } else if (act.type == ACT_SHOP_DEBUG) {
        // РАЗВЕДКА. Ничего в игре не меняет: только выясняет, где лежит
        // товар лавки, и пишет находки в SentientSands_SDK.log. Нужна,
        // чтобы строить настоящую выдачу по фактам, а не по догадкам.
        int countersFound = 0;
        int itemsSeen = 0;

        if (!npc || (uintptr_t)npc < 0x1000) {
          Log("SHOP_DEBUG: собеседник не найден");
          thisptr->showPlayerAMessage_withLog(
              "SHOP_DEBUG: собеседник не найден", true);
        } else {
          Log("SHOP_DEBUG: ===== " + npc->getName() +
              " handle=" + npc->getHandle().toString());

          // Перечислить содержимое инвентаря в лог. Имена — не более 40
          // штук, чтобы не раздувать файл на больших лавках.
          auto dumpInventory = [&](Inventory *inv, const std::string &label) {
            if (!inv || (uintptr_t)inv < 0x1000) {
              Log("SHOP_DEBUG:   " + label + ": инвентаря нет");
              return;
            }
            lektor<InventorySection *> &sections = inv->sectionsInSearchOrder;
            int cnt = 0;
            std::string names = "";
            for (uint32_t s = 0; s < sections.size(); ++s) {
              InventorySection *sect = sections[s];
              if (!sect)
                continue;
              const Ogre::vector<InventorySection::SectionItem>::type &items =
                  sect->getItems();
              for (uint32_t i = 0; i < items.size(); ++i) {
                if (!items[i].item)
                  continue;
                cnt++;
                if (cnt <= 40) {
                  if (!names.empty())
                    names += ", ";
                  names += items[i].item->getName();
                }
              }
            }
            itemsSeen += cnt;
            Log("SHOP_DEBUG:   " + label + ": предметов " + ToString(cnt) +
                (names.empty() ? std::string("") : (" [" + names + "]")));
          };

          // Осмотреть мебель-прилавок: как называется и что в ней лежит.
          // Поле shopOwner лежит в UseableStuff, а его заголовок тянет за
          // собой MyGUI и InventoryLayout — в этом файле их нет. Владельца
          // выясняем иначе: домашняя мебель отряда (источник 3 ниже) сама по
          // себе принадлежит этому NPC.
          auto dumpCounter = [&](Building *b, const std::string &origin,
                                 int idx) {
            if (!b || (uintptr_t)b < 0x1000)
              return;
            countersFound++;
            Log("SHOP_DEBUG:  прилавок [" + origin + " #" + ToString(idx) +
                "] '" + b->getName() + "' isAShop=" +
                ToString(b->isAShop() ? 1 : 0));
            dumpInventory(b->getInventory(),
                          "товар прилавка " + origin + " #" + ToString(idx));
          };

          // 1. Личный инвентарь — то единственное, что видит GIVE_ITEM сейчас.
          dumpInventory(npc->getInventory(), "личный инвентарь NPC");

          // 2. Здание, внутри которого стоит NPC, и его мебель-прилавки.
          hand insideHand = npc->isIndoors();
          Building *inside =
              insideHand.isValid() ? insideHand.getBuilding() : NULL;
          if (inside && (uintptr_t)inside > 0x1000) {
            Log("SHOP_DEBUG: стоит в здании '" + inside->getName() +
                "' isAShop=" + ToString(inside->isAShop() ? 1 : 0));
            dumpInventory(inside->getInventory(), "инвентарь здания");

            lektor<Building *> counters;
            inside->findAllFurnitureWithFunction(counters, BF_SHOP);
            Log("SHOP_DEBUG: мебели BF_SHOP в здании: " +
                ToString((int)counters.size()));
            for (uint32_t c = 0; c < counters.size(); ++c)
              dumpCounter(counters[c], "здание", (int)c);
          } else {
            Log("SHOP_DEBUG: NPC не в здании (isIndoors пуст)");
          }

          // 3. Домашняя мебель отряда — на случай, если NPC отошёл от прилавка.
          Ownerships *own = npc->getOwnerships();
          if (own && (uintptr_t)own > 0x1000) {
            lektor<Building *> homeShops;
            own->getHomeFurnitureOfType(homeShops, BF_SHOP);
            Log("SHOP_DEBUG: домашней мебели BF_SHOP у отряда: " +
                ToString((int)homeShops.size()));
            for (uint32_t h = 0; h < homeShops.size(); ++h)
              dumpCounter(homeShops[h], "дом", (int)h);

            Building *homeB = own->_homeBuilding.isValid()
                                  ? own->_homeBuilding.getBuilding()
                                  : NULL;
            if (homeB && (uintptr_t)homeB > 0x1000) {
              Log("SHOP_DEBUG: домашнее здание '" + homeB->getName() +
                  "' isAShop=" + ToString(homeB->isAShop() ? 1 : 0));
              dumpInventory(homeB->getInventory(),
                            "инвентарь домашнего здания");
            }
          } else {
            Log("SHOP_DEBUG: у NPC нет Ownerships");
          }

          Log("SHOP_DEBUG: ===== итог: прилавков " + ToString(countersFound) +
              ", предметов всего " + ToString(itemsSeen));
          thisptr->showPlayerAMessage_withLog(
              "SHOP_DEBUG: прилавков " + ToString(countersFound) +
                  ", предметов " + ToString(itemsSeen) +
                  " — подробности в SentientSands_SDK.log",
              true);
        }
      } else if (act.type == ACT_SAY && npc) {
        bool isPC = npc->isPlayerCharacter();
        Log("ACTION_EXEC: SAY [" + npc->getName() + "]: " + act.message +
            (isPC ? " (PC)" : " (NPC)"));
        try {
          // 🚨 FIX: Removed endDialogue(true) and setInDialog(false).
          // Calling these resets the character's AI state and clears goals.
          // Since actions now fire before speech, calling this would
          // immediately cancel the task the NPC just received (e.g., Follow
          // Player). sayALine handles its own visual state.

          // If something tore the dialogue system down between the request
          // and the reply, the bubble would be drawn into nothing. Clearing
          // the in-dialog flag re-arms it without opening a vanilla window.
          if (npc->dialogue && (uintptr_t)npc->dialogue > 0x1000) {
            try {
              npc->dialogue->setInDialog(false);
            } catch (...) {
            }
          }

          // Primary method: sayALine (supports multiple lines/delays)
          npc->sayALine(act.message, true);

          // Speech bubbles used to vanish at random: sayALine hands the
          // line to the dialogue system, which may drop it if the character
          // is busy, mid-conversation or was just given an order. say()
          // draws the floating text directly, so call it every time rather
          // than only when the dialogue system is missing. A duplicate
          // bubble is far better than a silent one.
          float speed = thisptr->getFrameSpeedMultiplier();
          if (speed < 1.0f)
            speed = 1.0f;
          float duration = g_speechBubbleLife * speed;

          bool timerSet = false;
          if (npc->dialogue && (uintptr_t)npc->dialogue > 0x1000) {
            npc->dialogue->speechTextTimer = duration;
            npc->dialogue->speechTextTimer_forced = duration;
            timerSet = true;
          }

          try {
            npc->say(act.message);
          } catch (...) {
            Log("ACTION_EXEC: SAY (WARN): say() refused the line");
          }

          // say() can reset the timer, so write it again afterwards.
          if (npc->dialogue && (uintptr_t)npc->dialogue > 0x1000) {
            npc->dialogue->speechTextTimer = duration;
            npc->dialogue->speechTextTimer_forced = duration;
            timerSet = true;
          }
          if (!timerSet)
            Log("ACTION_EXEC: SAY (WARN): no dialogue system on " +
                npc->getName() + ", bubble life left to the engine");

        } catch (...) {
          Log("ACTION_EXEC: SAY (ERROR): Exception during sayALine/say");
        }
      } else if (npc) {
        if (act.type == ACT_ATTACK && target) {
          if (npc->getFaction() && npc->getFaction()->isThePlayer()) {
            PerformLeaveSquad(npc, thisptr, "");
            npc->clearAllAIGoals();
          }
          npc->attackTarget(target);
          npc->addGoal(MELEE_ATTACK, (RootObjectBase *)target);
          npc->reThinkCurrentAIAction();
          thisptr->showPlayerAMessage(npc->getName() + " is attacking!", false);
        } else if (act.type == ACT_JOIN_PARTY && thisptr->player) {
          // 🚨 STORE PREVIOUS JOBS AND HOME BEFORE RECRUITMENT
          // This allows them to go back to their original behavior upon
          // dismissal.
          unsigned int serial = npc->getHandle().serial;
          OriginState state;

          // Store Home context if available
          Ownerships *own = npc->getOwnerships();
          if (own) {
            state.homeTown =
                own->_homeTown ? own->_homeTown->getHandle() : hand();
            state.homeBuilding = own->_homeBuilding;
          }

          int jobCount = npc->getPermajobCount();
          for (int i = 0; i < jobCount; ++i) {
            OriginJob oj;
            oj.type = npc->getPermajob(i);
            // Default to null, we rely on home building for specific tasks
            oj.target = hand();
            oj.location = npc->getPosition();
            state.jobs.push_back(oj);
          }
          g_originJobs[serial] = state;

          thisptr->player->recruit(npc, false);
          thisptr->playNotification("ui_cat_change");
          thisptr->showPlayerAMessage_withLog(
              npc->getName() + " joined your squad.", true);
        } else if (act.type == ACT_LEAVE) {
          npc->clearPermajobs();
          npc->clearAllAIGoals();
          PerformLeaveSquad(npc, thisptr, act.message);

          // Restore stored original jobs if they exist
          unsigned int serial = npc->getHandle().serial;
          if (g_originJobs.count(serial)) {
            const OriginState &state = g_originJobs[serial];

            // Restore Home context
            Ownerships *own = npc->getOwnerships();
            if (own) {
              TownBase *town = state.homeTown.getTown();
              if (town)
                own->setHomeTown(town, npc->getPlatoon()->me->squadType);
              if (state.homeBuilding.isValid())
                own->setHomeBuilding(state.homeBuilding,
                                     npc->getPlatoon()->me->squadType);
            }

            for (size_t i = 0; i < state.jobs.size(); ++i) {
              RootObject *subject = state.jobs[i].target.getRootObject();

              // Special case for shopkeepers: use home building as subject if
              // target is missing
              if (!subject && state.jobs[i].type == STAND_AT_SHOPKEEPER_NODE) {
                subject = (RootObject *)state.homeBuilding.getBuilding();
              }

              npc->addJob(state.jobs[i].type, subject, false, true,
                          state.jobs[i].location);
            }
          }

          // Clear limiting orders (Passive/Hold) that might prevent movement
          npc->setStandingOrder((MessageForB::StandingOrder)13 /* PASSIVE */,
                                false);
          npc->setStandingOrder((MessageForB::StandingOrder)12 /* HOLD */,
                                false);

          if (npc->getPermajobCount() == 0) {
            TownBase *town = npc->getCurrentTownLocation();
            if (town) {
              npc->addJob(WANDER_TOWN, (RootObject *)town, false, false,
                          npc->getPosition());
              npc->addGoal(WANDER_TOWN, (RootObjectBase *)town);
            } else {
              npc->addJob(WANDERER, NULL, false, false, npc->getPosition());
              npc->addGoal(WANDERER, NULL);
            }
          }
          npc->reThinkCurrentAIAction();
          thisptr->showPlayerAMessage_withLog(
              npc->getName() + " left your squad.", true);

        } else if (act.type == ACT_SET_TASK) {
          Log("ACTION_EXEC: Setting task for " + npc->getName() + ": " +
              ToString(act.taskValue) +
              (target ? " (Target: " + target->getName() + ")" : ""));

          // 🚨 DO NOT call endDialogue here — it kills the speech bubble that
          // the NPC just displayed. The dialogue system will clear naturally.

          // Clear limiting orders (Passive/Hold) that might prevent task
          // execution Matches enum values in MessageForB::StandingOrder
          npc->setStandingOrder((MessageForB::StandingOrder)13 /* PASSIVE */,
                                false);
          npc->setStandingOrder((MessageForB::StandingOrder)12 /* HOLD */,
                                false);

          npc->clearAllAIGoals();

          TaskType tt = (TaskType)act.taskValue;
          RootObject *taskTarget = (RootObject *)target;

          // SPECIAL HANDLING: If told to travel or raid a specific town
          if ((tt == TRAVEL_TO_TARGET_TOWN || tt == ATTACK_TOWN ||
               (int)tt == 18) &&
              !act.message.empty()) {
            std::string tName = act.message;
            // Cleanup quotes and whitespace
            size_t fnot = tName.find_first_not_of(" \t\n\r\"'");
            if (fnot != std::string::npos) {
              tName.erase(0, fnot);
              size_t lnot = tName.find_last_not_of(" \t\n\r\"'");
              if (lnot != std::string::npos)
                tName.erase(lnot + 1);
            }

            Log("ACTION_EXEC: Resolving town target for " + ToString(tt) +
                ": '" + tName + "'");

            std::string tLow = tName;
            std::transform(tLow.begin(), tLow.end(), tLow.begin(), ::tolower);

            lektor<RootObject *> resultTowns;
            (*ppWorld)->getObjectsWithinSphere(resultTowns, npc->getPosition(),
                                               10000000.0f, TOWN, 500, NULL);
            for (uint32_t i = 0; i < resultTowns.size(); ++i) {
              TownBase *tb = (TownBase *)resultTowns[i];
              if (tb) {
                std::string tbName = ((RootObjectBase *)tb)->getName();
                std::transform(tbName.begin(), tbName.end(), tbName.begin(),
                               ::tolower);

                // Try exact match or contains
                if (tbName == tLow || tbName.find(tLow) != std::string::npos) {
                  taskTarget = (RootObject *)tb;
                  Log("ACTION_EXEC: Found town match: " +
                      ((RootObjectBase *)tb)->getName());
                  break;
                }
              }
            }
            if (!taskTarget) {
              Log("ACTION_EXEC: WARNING: Town '" + tName +
                  "' not found in 10M units!");
            }
          }

          // SPECIAL HANDLING: If told to patrol/wander/attack town, ensure use
          // town target not player target (only if we didn't just find a
          // specific one above)
          if ((tt == PATROL_TOWN || tt == WANDER_TOWN || tt == ATTACK_TOWN ||
               tt == GO_HOMEBUILDING || tt == STAND_AT_SHOPKEEPER_NODE) &&
              !taskTarget) {
            TownBase *town = npc->getCurrentTownLocation();
            if (town)
              taskTarget = (RootObject *)town;
          } else if (tt == IDLE || tt == WANDERER || tt == RUN_AWAY ||
                     tt == MOVE_ON_FREE_WILL || tt == MOVE_ON_FREE_WILL_FAST) {
            // These tasks shouldn't have the player as a target or they walk
            // into the player. Medic/Rescue should have a target to follow.
            taskTarget = NULL;
          }

          bool isPermanent =
              (tt == TRAVEL_TO_TARGET_TOWN || tt == ATTACK_TOWN ||
               (int)tt == 18 || // RAID_TOWN
               tt == PATROL_TOWN || tt == WANDER_TOWN ||
               tt == GO_HOMEBUILDING || tt == STAND_AT_SHOPKEEPER_NODE ||
               tt == JOB_MEDIC || tt == JOB_REPAIR_ROBOT ||
               tt == FIND_AND_RESCUE || tt == FOLLOW_PLAYER_ORDER ||
               tt == BODYGUARD);

          Log("ACTION_EXEC: Final Dispatch -> Task: " + ToString((int)tt) +
              " (" + GetTaskName(tt) + "), Target: " +
              (taskTarget ? ((RootObjectBase *)taskTarget)->getName()
                          : "NULL") +
              ", Permanent: " + (isPermanent ? "YES" : "NO"));

          if (tt == JOB_MEDIC || tt == FIND_AND_RESCUE ||
              tt == JOB_REPAIR_ROBOT) {
            // Bundle caregiver tasks: Rescue (lower priority) then Medic
            // (higher priority) Using shift=false with addJob prepends, so the
            // LAST one added becomes the current top priority.
            npc->addJob(FIND_AND_RESCUE, taskTarget, false, true,
                        npc->getPosition());
            npc->addJob(JOB_MEDIC, taskTarget, false, true, npc->getPosition());
            if (tt == JOB_REPAIR_ROBOT) {
              npc->addJob(JOB_REPAIR_ROBOT, taskTarget, false, true,
                          npc->getPosition());
            }
            thisptr->showPlayerAMessage(
                npc->getName() + " is now in caregiver mode (Medic & Rescue).",
                false);
          } else {
            npc->addJob(tt, taskTarget, false, isPermanent, npc->getPosition());
            thisptr->showPlayerAMessage(
                npc->getName() + " is now executing: " + GetTaskName(tt),
                false);
          }

          npc->addGoal(tt, (RootObjectBase *)taskTarget);
          npc->reThinkCurrentAIAction();
        } else if (act.type == ACT_DROP_ITEM) {
          std::vector<Item *> items;
          GetAllCharacterItems(npc, items);
          std::string targetName = act.message;
          // Cleanup quotes and whitespace
          size_t fnot = targetName.find_first_not_of(" \t\n\r\"'");
          if (fnot != std::string::npos) {
            targetName.erase(0, fnot);
            size_t lnot = targetName.find_last_not_of(" \t\n\r\"'");
            if (lnot != std::string::npos)
              targetName.erase(lnot + 1);
          }
          std::transform(targetName.begin(), targetName.end(),
                         targetName.begin(), ::tolower);

          for (uint32_t i = 0; i < items.size(); ++i) {
            std::string itemName = items[i]->getName();
            std::transform(itemName.begin(), itemName.end(), itemName.begin(),
                           ::tolower);
            if (itemName.find(targetName) != std::string::npos) {
              Log("ACTION_EXEC: Dropping item: " + items[i]->getName());
              npc->dropItem(items[i]);
              thisptr->showPlayerAMessage_withLog(
                  npc->getName() + " dropped " + items[i]->getName(), true);
              npc->reThinkCurrentAIAction();
              break;
            }
          }
        } else if (act.type == ACT_TAKE_ITEM) {
          Character *player =
              (thisptr->player && thisptr->player->playerCharacters.size() > 0)
                  ? thisptr->player->playerCharacters[0]
                  : nullptr;
          if (player) {
            std::string targetName = act.message;
            size_t fnot = targetName.find_first_not_of(" \t\n\r\"'");
            if (fnot != std::string::npos) {
              targetName.erase(0, fnot);
              size_t lnot = targetName.find_last_not_of(" \t\n\r\"'");
              if (lnot != std::string::npos)
                targetName.erase(lnot + 1);
            }
            std::string lowerTarget = targetName;
            std::transform(lowerTarget.begin(), lowerTarget.end(),
                           lowerTarget.begin(), ::tolower);

            int count = act.taskValue;
            if (count < 1)
              count = 1;
            int taken = 0;

            Log("ACTION_EXEC: NPC " + npc->getName() + " attempting to take " +
                ToString(count) + "x '" + targetName + "'");

            // Robust loop: Scan for one item at a time since removals can
            // reorganize inventory
            while (taken < count) {
              std::vector<Item *> pItems;
              GetAllCharacterItems(player, pItems);
              Item *found = nullptr;

              for (uint32_t i = 0; i < pItems.size(); ++i) {
                Item *it = pItems[i];
                if (!it)
                  continue;
                std::string itemName = it->getName();
                std::transform(itemName.begin(), itemName.end(),
                               itemName.begin(), ::tolower);
                if (itemName.find(lowerTarget) != std::string::npos) {
                  found = it;
                  break;
                }
              }

              if (found) {
                Log("ACTION_EXEC: Taking item (" + ToString(taken + 1) + "/" +
                    ToString(count) + "): " + found->getName());
                if (found->isEquipped)
                  player->unequipItem(found->inventorySection, found);
                Inventory *inv = found->getInventory();
                if (!inv)
                  inv = player->getInventory();
                Item *detached = inv ? inv->removeItemDontDestroy_returnsItem(
                                           found, found->quantity, false)
                                     : nullptr;
                bool success = npc->giveItem(detached ? detached : found, true, false);
                if (success) {
                  taken++;
                } else {
                  Log("ACTION_EXEC: NPC " + npc->getName() + " inventory full! Returning item to player.");
                  player->giveItem(detached ? detached : found, true, false);
                  break; // Stop taking items if we hit a full inventory
                }
              } else {
                // No more items matching this name
                break;
              }
            }

            if (taken < count) {
              transactionFailed = true;
              failureReason = "Not enough items.";
              Log("ACTION_EXEC: TRANSACTION FAILED: " + npc->getName() + " wanted " + ToString(count) + " but only found " + ToString(taken));
            }

            if (taken > 0) {
              std::string msg = npc->getName() + " took " +
                                (taken > 1 ? ToString(taken) + "x " : "") +
                                targetName + " from you.";
              thisptr->showPlayerAMessage_withLog(msg, true);
              npc->reThinkCurrentAIAction();
              inventoryTimer = 999;
            } else {
              Log("ACTION_EXEC: NPC " + npc->getName() +
                  " found NO items matching '" + targetName + "' on player.");
            }
          }
        } else if (act.type == ACT_GIVE_ITEM) {
          if (transactionFailed) {
            Log("ACTION_EXEC: Skipping GIVE_ITEM due to previous transaction failure (" + failureReason + ")");
            continue;
          }
          std::vector<Item *> items;
          GetAllCharacterItems(npc, items);
          std::string targetName = act.message;
          size_t fnot = targetName.find_first_not_of(" \t\n\r\"'");
          if (fnot != std::string::npos) {
            targetName.erase(0, fnot);
            size_t lnot = targetName.find_last_not_of(" \t\n\r\"'");
            if (lnot != std::string::npos)
              targetName.erase(lnot + 1);
          }
          std::string originalTargetName = targetName;
          std::transform(targetName.begin(), targetName.end(),
                         targetName.begin(), ::tolower);

          int count = act.taskValue;
          if (count < 1)
            count = 1;
          int given = 0;

          Character *player =
              (thisptr->player && thisptr->player->playerCharacters.size() > 0)
                  ? thisptr->player->playerCharacters[0]
                  : nullptr;

          if (player) {
            for (uint32_t i = 0; i < items.size() && given < count; ++i) {
              std::string itemName = items[i]->getName();
              std::transform(itemName.begin(), itemName.end(), itemName.begin(),
                             ::tolower);
              if (itemName.find(targetName) != std::string::npos) {
                Log("ACTION_EXEC: Giving item (" + ToString(given + 1) + "/" +
                    ToString(count) + "): " + items[i]->getName());
                if (items[i]->isEquipped)
                  npc->unequipItem(items[i]->inventorySection, items[i]);
                Inventory *inv = items[i]->getInventory();
                if (!inv)
                  inv = npc->getInventory();
                Item *detached = inv ? inv->removeItemDontDestroy_returnsItem(
                                           items[i], items[i]->quantity, false)
                                     : nullptr;
                if (detached) {
                  player->giveItem(detached, true, false);
                  given++;
                } else {
                  Log("ACTION_EXEC: Failed to detach " + items[i]->getName() +
                      " from " + npc->getName() + "'s inventory.");
                }
              }
            }

            if (given < count) {
              Log("ACTION_EXEC: NPC " + npc->getName() + " only had " +
                  ToString(given) + " of '" + originalTargetName +
                  "'. Fallback to SPAWN for remaining " +
                  ToString(count - given));
              itemType types[] = {ITEM,     WEAPON,    ARMOUR,
                                  CROSSBOW, BLUEPRINT, LIMB_REPLACEMENT,
                                  MAP_ITEM};
              GameData *gd = nullptr;
              for (int t = 0; t < 7; t++) {
                gd = thisptr->leveldata.getDataByName(originalTargetName,
                                                      types[t]);
                if (!gd)
                  gd = thisptr->gamedata.getDataByName(originalTargetName,
                                                       types[t]);
                if (gd)
                  break;
              }

              if (gd) {
                int toSpawn = count - given;
                for (int s = 0; s < toSpawn; s++) {
                  std::string uniqueID =
                      originalTargetName + "_AI_" +
                      ToString((unsigned int)GetTickCount()) + "_" +
                      ToString(s);
                  GameData *newGd = thisptr->savedata.createNewData(
                      gd->type, uniqueID, gd->name);
                  if (newGd) {
                    newGd->updateFrom(gd, true);
                    Item *spawned = thisptr->theFactory->createItem(
                        gd, hand(), NULL, NULL, 0, NULL);
                    if (spawned) {
                      spawned->quantity = 1;
                      spawned->setProperOwner(player->getHandle());
                      bool success = player->giveItem(spawned, true, false);
                      if (success)
                        given++;
                    }
                  }
                }
              }
            }

            if (given > 0) {
              std::string msg =
                  npc->getName() + " gave you " +
                  (given > 1 ? ToString(given) + "x " : "") +
                  (given > 1 ? originalTargetName : originalTargetName);
              thisptr->showPlayerAMessage_withLog(msg, true);
              npc->reThinkCurrentAIAction();
              inventoryTimer = 999;
            }
          }
        } else if (act.type == ACT_GIVE_CATS) {
          if (transactionFailed) {
            Log("ACTION_EXEC: Skipping GIVE_CATS due to previous transaction failure (" + failureReason + ")");
            continue;
          }
          if (npc && thisptr->player &&
                   thisptr->player->playerCharacters.size() > 0) {
            int amt = act.taskValue;
            // Больше, чем есть при себе, персонаж отдать не может — иначе под
            // угрозой из нищего бродяги можно выдоить любые суммы, и его счёт
            // уйдёт в минус. Отдаём остаток кассы, а не выдуманное число.
            if (amt > 0) {
              int purse = npc->getMoney();
              if (purse <= 0 && npc->getOwnerships())
                purse = npc->getOwnerships()->getMoney();
              if (purse < 0)
                purse = 0;
              if (amt > purse) {
                Log("ACTION_EXEC: GIVE_CATS capped " + ToString(amt) + " -> " +
                    ToString(purse) + " (that is all " + npc->getName() +
                    " carries)");
                amt = purse;
              }
            }
            if (amt > 0) {
              thisptr->player->playerCharacters[0]->takeMoney(-amt);

              // Avoid no-op transfers to characters already in player faction
              bool alreadyPlayer =
                  (npc && npc->getFaction() && npc->getFaction()->isThePlayer());
              if (npc && !alreadyPlayer)
                npc->takeMoney(amt);

              thisptr->showPlayerAMessage_withLog(
                  "Gained " + ToString(amt) + " cats.", true);
            }
          }
        } else if (act.type == ACT_SURRENDER) {
          // ── Сложить оружие ────────────────────────────────────────────────
          // Смены задачи на «замри» недостаточно: боец с непогашенной враждой
          // тут же возвращается в драку. Поэтому сперва снимаем вражду к
          // игроку и его людям — тем же способом, что применяется при выкупе
          // раба, — и лишь затем ставим задачу стоять.
          // Персонаж уже получен в начале цикла — берём его.
          Character *sn = npc;
          if (sn && (uintptr_t)sn > 0x1000) {
            int cleared = 0;
            try {
              if (thisptr->player) {
                for (uint32_t pi = 0;
                     pi < thisptr->player->playerCharacters.size(); ++pi) {
                  Character *pc = thisptr->player->playerCharacters[pi];
                  if (!pc || (uintptr_t)pc < 0x1000)
                    continue;
                  sn->clearTempEnemyStatus(pc);   // он больше не враг игроку
                  pc->clearTempEnemyStatus(sn);   // и игрок ему — тоже
                  ++cleared;
                }
              }
              sn->clearAllTempEnemyStatuses(ST_AGGRESSOR);
              sn->clearAllTempEnemyStatuses(ST_INTRUDER);
              sn->clearAllTempEnemyStatuses(ST_CRIMINAL);
              sn->clearAllTempEnemyStatuses(ST_TEMPORARY_ENEMY);
            } catch (...) {
            }
            // Метки восприятия — это лишь «кто мне враг». Сам боевой настрой
            // живёт отдельно, и потому здоровые бойцы возвращались в драку.
            // У движка для этого есть собственный переключатель: пометить
            // персонажа временно нейтральным и обнулить его агрессию.
            try {
              StateBroadcastData *sb = sn->getStateBroadcast();
              if (sb && (uintptr_t)sb > 0x1000) {
                sb->isTemporaryNeutral = true;
                sb->aggressionTowardsTarget = 0.0f;
                sb->intendedAggression = 0.0f;
                sb->distress = 1.0f;   // он напуган и хочет убраться
              }
            } catch (...) {
            }
            // ГЛАВНОЕ. Диагностика показала: нейтралитет ставится и агрессия
            // обнуляется, но у стоящих в боевой стойке ОСТАЁТСЯ ЦЕЛЬ АТАКИ, и
            // они продолжают драку. Убегали только те, у кого цели не было.
            // Значит, надо вывести из боевого режима напрямую и убрать оружие
            // — у движка для этого есть свои методы.
            try {
              sn->endCombatMode();
            } catch (...) {
            }
            try {
              sn->sheatheWeapon();
            } catch (...) {
            }
            try {
              // ВКЛЮЧАЕМ пассивный режим (13) — именно он запрещает лезть в
              // драку. Прежде я по ошибке его СНИМАЛ: строка была скопирована
              // из кода, который наоборот убирает ограничения перед выдачей
              // задачи. Оттого здоровые бойцы и продолжали бой — отступали
              // только те, кто и сам уже хотел бежать.
              sn->setStandingOrder((MessageForB::StandingOrder)13, true);
              // Удержание позиции (12) снимаем: оно мешало бы отступать.
              sn->setStandingOrder((MessageForB::StandingOrder)12, false);
              // Оборонительный бой вместо наступательного.
              sn->setStandingOrder((MessageForB::StandingOrder)11, true);
              sn->setStandingOrder((MessageForB::StandingOrder)5, false);
              sn->clearAllAIGoals();
              // «Замри» оказалось слабее боевого поведения: персонаж дёргался
              // и возвращался в драку. Принудительное бегство перебивает бой,
              // поэтому сдавшийся отступает, а не стоит столбом. Ставим
              // задачу ПОСТОЯННОЙ (четвёртый аргумент), чтобы ИИ не сбросил
              // её на следующем же тике и не взялся за старое.
              // Задачу ставим по рабочему образцу мода: не только addJob, но
              // и addGoal, а в конце — reThinkCurrentAIAction. Без последнего
              // ИИ не пересматривает поведение и продолжает прежнее занятие,
              // то есть драку. Именно этого шага мне и не хватало.
              sn->clearPermajobs();
              TownBase *home = sn->getCurrentTownLocation();
              if (home) {
                // Уйти в город — задача сильная и с понятной целью.
                sn->addJob(RUN_AWAY_HOMETOWN, (RootObject *)home, false, true,
                           sn->getPosition());
                sn->addGoal(RUN_AWAY_HOMETOWN, (RootObjectBase *)home);
              } else {
                sn->addJob(RUN_AWAY_FORCED, NULL, false, true,
                           sn->getPosition());
                sn->addGoal(RUN_AWAY_FORCED, NULL);
                sn->addJob(RUN_AWAY, NULL, false, true, sn->getPosition());
                sn->addGoal(RUN_AWAY, NULL);
              }
              sn->reThinkCurrentAIAction();
            } catch (...) {
            }
            Log("ACTION_EXEC: SURRENDER — " + sn->getName() +
                " сложил оружие и отступает (погашено связей: " + ToString(cleared) + ")");
            thisptr->showPlayerAMessage_withLog(
                sn->getName() + " складывает оружие и отступает.", true);
          }
        } else if (act.type == ACT_TAKE_CATS) {
          if (transactionFailed) {
            Log("ACTION_EXEC: Skipping TAKE_CATS due to failed delivery (" +
                failureReason + ")");
            if (npc)
              thisptr->showPlayerAMessage(
                  npc->getName() + ": \"Сделка не вышла. Денег не возьму.\"",
                  true);
            continue;
          }
          Character *p =
              (thisptr->player && thisptr->player->playerCharacters.size() > 0)
                  ? thisptr->player->playerCharacters[0]
                  : nullptr;
          if (p) {
            int targetAmt = act.taskValue;
            int pMoney = p->getMoney();
            if (pMoney <= 0 && p->getOwnerships())
              pMoney = p->getOwnerships()->getMoney();

            int amt = targetAmt;
            if (amt > pMoney) {
              amt = pMoney;
              transactionFailed = true;
              failureReason = "Not enough cats.";
            }
            if (amt < 1) {
              amt = 0;
            }

            Log("ACTION_EXEC: Taking " + ToString(amt) + " cats from " +
                p->getName() + " (Requested: " + ToString(targetAmt) +
                ", Bank: " + ToString(pMoney) + ")");
            p->takeMoney(amt);

            // 🚨 RECRUITMENT FEE PROTECTION
            // If the NPC is also being recruited in this same batch, do NOT
            // give the refund to their new player pocket.
            bool beingRecruited = false;
            for (size_t i = 0; i < localQueue.size(); ++i) {
              if (localQueue[i].type == ACT_JOIN_PARTY &&
                  localQueue[i].actor == act.actor) {
                beingRecruited = true;
                break;
              }
            }

            bool alreadyPlayer =
                (npc && npc->getFaction() && npc->getFaction()->isThePlayer());

            if (npc && !beingRecruited && !alreadyPlayer) {
              npc->takeMoney(-amt);
            } else {
              Log("ACTION_EXEC: Recruitment fee or sign-on bonus. Money spent "
                  "but not given to recruit pocket.");
            }

            thisptr->showPlayerAMessage_withLog(
                "Lost " + ToString(amt) + " cats.", true);

            if (transactionFailed) {
              thisptr->showPlayerAMessage(
                  npc->getName() +
                      " looks annoyed. \"That's not what we agreed on!\"",
                  true);
            }
          }
        } else if (act.type == ACT_RELEASE && npc && target) {
          // IN_PRISON is enum value 2
          bool inCage = (target->inSomething == 2);
          bool shackled = target->isChained || target->isChainedMode();
          float dist = npc->getPosition().distance(target->getPosition());

          Log("ACTION_EXEC: Release/Breakout by " + npc->getName() + " on " +
              target->getName() + ". InCage: " + ToString(inCage) +
              ", Shackled: " + ToString(shackled) +
              ", Dist: " + ToString(dist));

          // FORCE EXECUTION IF CLOSE
          // This bypasses the engine task clearing (crouch & clear) for
          // recruits/friends.
          // Self-release (a bought slave leaving his own cage) has actor
          // and target as the same character, so distance is always 0.
          bool selfRelease = (npc == target);
          // Take the slave status off BEFORE the cage opens. The guards
          // react to the act of freeing a slave, not to the bounty that
          // follows it: by the time the bounty was wiped they had already
          // drawn their swords. If the man is nobody's property when the
          // door swings open, there is no crime to witness.
          if (selfRelease) {
            try {
              // Hand the slave over to the buyer instead of wiping the
              // owner. An ownerless slave IS a runaway as far as the game
              // is concerned — that is exactly what the guards were
              // reacting to. Vanilla Kenshi transfers ownership on a
              // purchase, and an owner opening his own cage commits no
              // crime.
              // SlaveStateEnum: NOT_SLAVE, IS_SLAVE, ESCAPING_SLAVE,
              // EX_SLAVE. Only the first one means a free person.
              // ESCAPING_SLAVE is a runaway and EX_SLAVE is a runaway
              // lying low — both get re-flagged once the guards take a
              // good look. A man the player paid for was never on the run.
              StateBroadcastData *sb = target->getStateBroadcast();
              if (sb) {
                sb->setSlaveState(NOT_SLAVE);
                sb->isEscapedPrisoner = false;
                sb->looksLike_escapedSlave = 0.0f;
                sb->looksLike_bounty = 0.0f;
                sb->isSlaveOf = nullptr;
                Log("ACTION_EXEC: Slave state set to NOT_SLAVE for " +
                    target->getName());
              }
              target->changeSlaveOwner(hand());
              target->slaveOwner = hand();
              target->setSlaveAIJob(false);
              target->crimes.committingCrime = CRIME_NONE;
              target->crimes.crimeAgainstFaction = nullptr;
              target->crimes.crimeExpiry = 0.0f;

              FreedSlaveWatch w;
              w.who = target->getHandle();
              w.ticksLeft = 600; // roughly ten seconds of frames
              g_freedSlaves.push_back(w);
              Log("ACTION_EXEC: Watching " + target->getName() +
                  " for a delayed runaway bounty");

              // Guards who saw the cage open tag the freed man and the
              // player as criminals, and that tag is what sends them into
              // combat. Clearing the bounty afterwards does not calm them
              // down, so wipe the tags on everyone standing around.
              if (ppWorld && *ppWorld) {
                Character *buyer = nullptr;
                if (thisptr->player &&
                    thisptr->player->playerCharacters.size() > 0)
                  buyer = thisptr->player->playerCharacters[0];

                lektor<RootObject *> witnesses;
                (*ppWorld)->getCharactersWithinSphere(
                    witnesses, target->getPosition(), 60.0f, 0.0f, 0.0f, 100,
                    0, nullptr);
                int calmed = 0;
                for (uint32_t wi = 0; wi < witnesses.size(); ++wi) {
                  Character *ob = (Character *)witnesses.stuff[wi];
                  if (!ob || (uintptr_t)ob < 0x1000)
                    continue;
                  try {
                    ob->clearTempEnemyStatus(target);
                    if (buyer)
                      ob->clearTempEnemyStatus(buyer);
                    ob->clearAllTempEnemyStatuses(ST_CRIMINAL);
                    ob->clearAllTempEnemyStatuses(ST_INTRUDER);
                    ++calmed;
                  } catch (...) {
                  }
                }
                Log("ACTION_EXEC: Cleared criminal tags on " +
                    ToString(calmed) + " witnesses");

                // The buyer gets charged for the freeing as well, and the
                // guards went for HIM. Clear the whole squad right away.
                if (thisptr->player) {
                  for (size_t pi = 0;
                       pi < thisptr->player->playerCharacters.size(); ++pi) {
                    Character *sq = thisptr->player->playerCharacters[pi];
                    if (!sq || (uintptr_t)sq < 0x1000)
                      continue;
                    std::vector<Faction *> pf;
                    ogre_unordered_map<Faction *, Bounty>::type::iterator pit =
                        sq->crimes.bounties.begin();
                    for (; pit != sq->crimes.bounties.end(); ++pit) {
                      if (pit->first)
                        pf.push_back(pit->first);
                    }
                    for (size_t bi = 0; bi < pf.size(); ++bi)
                      sq->crimes.clearBounty(pf[bi]);
                    sq->crimes.committingCrime = CRIME_NONE;
                    sq->crimes.crimeAgainstFaction = nullptr;
                    sq->crimes.crimeExpiry = 0.0f;
                  }
                  Log("ACTION_EXEC: Cleared bounties on the player squad");
                }
              }
            } catch (...) {
              Log("ACTION_EXEC: WARN - could not clear slave status");
            }
          }

          bool freedNow = false;
          if ((selfRelease || dist < 4.0f) && (inCage || shackled)) {
            freedNow = true;
            Log(selfRelease ? "ACTION_EXEC: Self-release triggered."
                            : "ACTION_EXEC: Proximity force-release triggered.");
            if (shackled) {
              target->setChainedMode(false, hand());
              target->isChained = false;
            }
            if (inCage) {
              target->setPrisonMode(false, nullptr);
              // Manually clear the enclosure state if setPrisonMode isn't
              // enough
              target->inSomething = (UseStuffState)0; // IN_NOTHING
            }
            thisptr->showPlayerAMessage("You have been freed!", true);

            // Re-assert the freed state: opening the cage can flip it back.
            if (selfRelease) {
              try {
                StateBroadcastData *sb2 = target->getStateBroadcast();
                if (sb2 && sb2->getSlaveState() != NOT_SLAVE) {
                  sb2->setSlaveState(NOT_SLAVE);
                  sb2->isEscapedPrisoner = false;
                  sb2->looksLike_escapedSlave = 0.0f;
                  Log("ACTION_EXEC: Re-applied NOT_SLAVE after opening the cage");
                }
              } catch (...) {
              }
            }
          }

          bool didSomething = false;

          // 1. Handle Carrying (Drop first)
          if (npc->isCarryingSomething &&
              npc->carryingObject == target->getHandle()) {
            Log("ACTION_EXEC: NPC is carrying target. Dropping.");
            npc->dropCarriedObject(false, false);
            didSomething = true;
          }

          // 2. Handle Imprisonment (Cage/Shackles)
          // A self-release is already done above: ordering a character to
          // run a RELEASE_PRISONER task on himself would just leave him
          // standing in place.
          if (!selfRelease && (inCage || shackled)) {
            // Identify the best task
            TaskType tt = RELEASE_PRISONER; // Default 110
            if (act.taskValue == 111) {
              tt = BREAKOUT_PRISONER; // 111
              if (shackled && !inCage)
                tt = (TaskType)201; // PICK_LOCK_ON_SHACKLES
            } else if (shackled && !inCage) {
              tt = RELEASE_PRISONER; // Usually handles legal unshackling
            }

            Log("ACTION_EXEC: Assigning task: " + GetTaskName(tt) + " (" +
                ToString((int)tt) + ")");

            // Use addOrder (immediate override) instead of addJob
            // The clear=true flag stops background AI like "Staying home"
            npc->clearAllAIGoals();
            npc->addOrder(nullptr, tt, (RootObject *)target, false, true,
                          target->getPosition());
            npc->reThinkCurrentAIAction();

            thisptr->showPlayerAMessage(npc->getName() +
                                            (act.taskValue == 111
                                                 ? " is breaking out "
                                                 : " is releasing ") +
                                            target->getName() + "!",
                                        false);
            didSomething = true;
          }

          if (!didSomething && !freedNow && !npc->isPlayerCharacter()) {
            Log("ACTION_EXEC: Target already free. Clearing NPC goals.");
            npc->clearAllAIGoals();
            npc->reThinkCurrentAIAction();
          }
        } else if (act.type == ACT_FACTION_RELATIONS) {
          if (thisptr->factionMgr) {
            Faction *targetFaction =
                thisptr->factionMgr->getFactionByStringID(act.message);
            if (!targetFaction)
              targetFaction =
                  thisptr->factionMgr->getFactionByName(act.message);
            Faction *playerFaction =
                thisptr->player ? thisptr->player->getFaction() : nullptr;
            if (!playerFaction)
              playerFaction =
                  thisptr->factionMgr->getFactionByStringID("Nameless_0");

            if (targetFaction && playerFaction) {
              Log("ACTION_EXEC: Direct Faction Relation change: " +
                  act.message + " (" + ToString(act.taskValue) + ")");
              if (playerFaction->relations)
                playerFaction->relations->affectRelations(
                    targetFaction, (float)act.taskValue, 1.0f);
              if (targetFaction->relations)
                targetFaction->relations->affectRelations(
                    playerFaction, (float)act.taskValue, 1.0f);
              thisptr->showPlayerAMessage_withLog(
                  "Political clout shift: Relationship with " +
                      targetFaction->getName() + " modified.",
                  true);
            }
          }
        } else if (act.type == ACT_SPAWN_ITEM) {
          // 🚨 NOTE: ACT_SPAWN_ITEM does NOT require npc to be valid.
          // It only needs thisptr (GameWorld). This block is intentionally
          // at this level, not nested inside 'else if (npc)'.
          std::string payload = act.message;

          // 🚨 SAFETY: Some LLM responses or test commands might double-up the
          // prefix. Strip redundant "SPAWN_ITEM:" from the payload if present.
          if (payload.find("SPAWN_ITEM:") == 0) {
            payload = payload.substr(11);
            size_t first = payload.find_first_not_of(" \t\r\n");
            if (first != std::string::npos)
              payload = payload.substr(first);
          }

          std::string templateName, itemName, itemDesc;
          size_t pipe1 = payload.find('|');
          if (pipe1 != std::string::npos) {
            templateName = payload.substr(0, pipe1);
            size_t pipe2 = payload.find('|', pipe1 + 1);
            if (pipe2 != std::string::npos) {
              itemName = payload.substr(pipe1 + 1, pipe2 - pipe1 - 1);
              itemDesc = payload.substr(pipe2 + 1);
            } else {
              itemName = payload.substr(pipe1 + 1);
            }
          } else {
            templateName = payload;
          }

          auto trim = [](std::string &s) {
            size_t first = s.find_first_not_of(" \t\n\r\"'");
            if (first == std::string::npos) {
              s = "";
              return;
            }
            s.erase(0, first);
            size_t last = s.find_last_not_of(" \t\n\r\"'");
            if (last != std::string::npos)
              s.erase(last + 1);

            // Normalize internal whitespace (e.g. \n or multiple spaces) to a
            // single space
            for (size_t i = 0; i < s.length(); ++i) {
              if (s[i] == '\r' || s[i] == '\n' || s[i] == '\t')
                s[i] = ' ';
            }
            // Collapse multiple spaces
            size_t p = s.find("  ");
            while (p != std::string::npos) {
              s.erase(p, 1);
              p = s.find("  ");
            }
          };
          trim(templateName);
          trim(itemName);
          trim(itemDesc);

          // --- ROBUST LOOKUP ---
          // Try direct, then plural, then substring
          itemType types[] = {ITEM,      WEAPON,           ARMOUR,  CROSSBOW,
                              BLUEPRINT, LIMB_REPLACEMENT, MAP_ITEM};
          GameData *gd = nullptr;

          auto findInSource = [&](GameDataManager &dm,
                                  const std::string &name) -> GameData * {
            for (int i = 0; i < 7; i++) {
              GameData *found = dm.getDataByName(name, types[i]);
              if (found)
                return found;
            }
            // Case-insensitive fallback pass
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(),
                           lowerName.begin(), ::tolower);
            boost::unordered::unordered_map<std::string, GameData *>::iterator
                it;
            for (it = dm.gamedataSID.begin(); it != dm.gamedataSID.end();
                 ++it) {
              GameData *check = it->second;
              if (check && !check->name.empty()) {
                std::string lowerCheck = check->name;
                std::transform(lowerCheck.begin(), lowerCheck.end(),
                               lowerCheck.begin(), ::tolower);
                if (lowerCheck == lowerName) {
                  for (int t = 0; t < 7; t++) {
                    if (check->type == types[t])
                      return check;
                  }
                }
              }
            }
            return (GameData *)nullptr;
          };

          gd = findInSource(thisptr->leveldata, templateName);
          if (!gd)
            gd = findInSource(thisptr->gamedata, templateName);

          if (!gd) {
            // Try plural
            std::string plural = templateName + "s";
            gd = findInSource(thisptr->leveldata, plural);
            if (!gd)
              gd = findInSource(thisptr->gamedata, plural);
          }

          if (!gd) {
            // Substring search (slow fallback)
            std::string lowerTemplate = templateName;
            std::transform(lowerTemplate.begin(), lowerTemplate.end(),
                           lowerTemplate.begin(), ::tolower);
            boost::unordered::unordered_map<std::string, GameData *>::iterator
                it;
            for (it = thisptr->gamedata.gamedataSID.begin();
                 it != thisptr->gamedata.gamedataSID.end(); ++it) {
              GameData *check = it->second;
              if (check && !check->name.empty()) {
                std::string lowerName = check->name;
                std::transform(lowerName.begin(), lowerName.end(),
                               lowerName.begin(), ::tolower);
                if (lowerName.find(lowerTemplate) != std::string::npos) {
                  // Ensure it's an item type
                  for (int t = 0; t < 7; t++) {
                    if (check->type == types[t]) {
                      gd = check;
                      break;
                    }
                  }
                  if (gd)
                    break;
                }
              }
            }
          }

          if (gd) {
            Log("ACTION_EXEC: Resolved " + templateName + " to " + gd->name +
                " (Type: " + ToString(gd->type) + ")");

            Character *p = act.target.getCharacter();
            if (!p || (uintptr_t)p < 0x1000) {
              if (thisptr->player &&
                  thisptr->player->playerCharacters.size() > 0)
                p = thisptr->player->playerCharacters[0];
            }

            if (p) {
              int count = act.taskValue;
              if (count < 1)
                count = 1;
              int spawnedCount = 0;

              for (int c = 0; c < count; c++) {
                Log("ACTION_EXEC: Spawning " + gd->name + " (" +
                    ToString(c + 1) + "/" + ToString(count) + ") for " +
                    p->getName());

                // 🛠️ FIX: Weapons, Armor, and Crossbows need specific
                // manufacturer/material data.
                GameData *meshData = nullptr;
                GameData *materialData = nullptr;

                if (gd->type == WEAPON || gd->type == ARMOUR ||
                    gd->type == CROSSBOW) {
                  // 1. Try to find references in the item template itself
                  auto getRef = [&](const std::string &refName) -> GameData * {
                    const Ogre::vector<GameDataReference>::type *refs =
                        gd->getReferenceListIfExists(refName);
                    if (refs && !refs->empty()) {
                      GameData *r = thisptr->gamedata.getData(refs->at(0).sid);
                      if (r) {
                        Log("ACTION_EXEC: Found " + refName + " ref: " +
                            r->name + " (Type: " + ToString(r->type) + ")");
                      }
                      return r;
                    }
                    return nullptr;
                  };

                  int requiredMeshType = 0;
                  if (gd->type == WEAPON)
                    requiredMeshType = MATERIAL_SPECS_WEAPON;
                  else if (gd->type == ARMOUR)
                    requiredMeshType = MATERIAL_SPECS_CLOTHING;

                  // 🛠️ FIX: Weapons use "material" for the grade/quality,
                  // while "mesh" is visual. The factory's 3rd arg expects the
                  // grade data (MATERIAL_SPECS_WEAPON or CLOTHING).
                  meshData = getRef("material");
                  if (!meshData ||
                      (requiredMeshType && meshData->type != requiredMeshType))
                    meshData = getRef("model");
                  if (!meshData ||
                      (requiredMeshType && meshData->type != requiredMeshType))
                    meshData = getRef("mesh");

                  materialData = getRef("manufacturer");

                  // 2. Fallback: Search global gamedata for "Standard" versions
                  // if template lacks them OR if the resolved ref is the wrong
                  // type.
                  bool needsMesh =
                      !meshData ||
                      (requiredMeshType && meshData->type != requiredMeshType);
                  bool needsMat = !materialData ||
                                  (materialData->type != WEAPON_MANUFACTURER);

                  if (needsMesh || needsMat) {
                    GameData *firstMesh = nullptr;
                    GameData *firstMat = nullptr;
                    boost::unordered::unordered_map<std::string,
                                                    GameData *>::iterator it;

                    for (it = thisptr->gamedata.gamedataSID.begin();
                         it != thisptr->gamedata.gamedataSID.end(); ++it) {
                      GameData *check = it->second;
                      if (!check)
                        continue;

                      if (needsMesh && (!requiredMeshType ||
                                        check->type == requiredMeshType)) {
                        if (!firstMesh)
                          firstMesh = check;
                        if (check->name.find("Standard") != std::string::npos ||
                            check->name.find("Catun") != std::string::npos)
                          meshData = check;
                      }

                      if (needsMat && check->type == WEAPON_MANUFACTURER) {
                        if (!firstMat)
                          firstMat = check;
                        if (check->name.find("Skeleton Smiths") !=
                                std::string::npos ||
                            check->name.find("Standard") != std::string::npos)
                          materialData = check;
                      }

                      if ((!needsMesh || meshData) &&
                          (!needsMat || materialData))
                        break;
                    }

                    // Final Fail-safe: If preferred name not found, take the
                    // first one
                    if (needsMesh && !meshData)
                      meshData = firstMesh;
                    if (needsMat && !materialData)
                      materialData = firstMat;
                  }
                }

                Item *item = thisptr->theFactory->createItem(
                    gd, hand(), meshData, materialData, 0, NULL);
                if (item) {
                  item->quantity = 1;
                  item->quality = 1.0f;
                  // Ensure food is full
                  item->chargesLeft = item->originalFullChargeAmount;
                  if (item->chargesLeft <= 0.0f)
                    item->chargesLeft = 1.0f;

                  item->setProperOwner(p->getHandle());
                  item->visible = true;
                  item->isTradeItem = true;

                  if (!item->container && p->container) {
                    item->container = p->container;
                    p->container->addActiveObject(item);
                  }

                  bool success = p->giveItem(item, true, false);
                  if (success) {
                    spawnedCount++;
                    Log("ACTION_EXEC: Successfully spawned and gave " +
                        gd->name + " to " + p->getName());
                  } else {
                    Log("ACTION_EXEC: WARNING: Resolved " + gd->name +
                        " but giveItem failed (inventory full?) for " +
                        p->getName());
                  }
                } else {
                  Log("ACTION_EXEC: ERROR: Resolved " + gd->name +
                      " but Factory failed to createItem! (Mesh: " +
                      (meshData ? meshData->name : "NULL") + ", Mat: " +
                      (materialData ? materialData->name : "NULL") + ")");
                }
              }

              if (spawnedCount > 0) {
                if (p->getInventory()) {
                  p->getInventory()->autoArrange();
                  p->getInventory()->refreshGui();
                }
                thisptr->addPortraitUpdate(p->getHandle());
                thisptr->showPlayerAMessage_withLog(
                    "Received " +
                        (spawnedCount > 1 ? ToString(spawnedCount) + "x "
                                          : "") +
                        (itemName.empty() ? gd->name : itemName),
                    true);
                inventoryTimer = 999;
              }
            } else {
              Log("ACTION_EXEC: No character found to give item to.");
              // No player found — drop item at NPC position as fallback
              Ogre::Vector3 dropPos = (npc && (uintptr_t)npc > 0x1000)
                                          ? npc->getPosition()
                                          : Ogre::Vector3::ZERO;
              dropPos.y += 2.0f; // Raise drop height

              Log("ACTION_EXEC: No player character found, dropping item at "
                  "NPC/Origin.");

              Item *item = thisptr->theFactory->createItem(gd, hand(), NULL,
                                                           NULL, 1, NULL);
              if (item) {
                item->quantity = 1;
                item->activate(true, dropPos, Ogre::Quaternion::IDENTITY, false,
                               YesNoMaybe::NO, true);
                thisptr->showPlayerAMessage_withLog(
                    "Item dropped nearby: " + templateName, true);
              }
            }
          } else {
            Log("ACTION_EXEC: Could not find template for: " + templateName);
            thisptr->showPlayerAMessage(
                "Error: Item template '" + templateName + "' not found.", true);
            // Выдача сорвалась — помечаем сделку неудачной, чтобы следом НЕ
            // списать деньги: иначе игрок платит и остаётся ни с чем.
            transactionFailed = true;
            failureReason = "Item not found: " + templateName;
            Log("ACTION_EXEC: SPAWN_ITEM failed — payment will be skipped.");
          }
        }
      }
    } catch (...) {
      Log("ACTION_EXEC: CRITICAL EXCEPTION during action index " +
          ToString((int)actIdx));
    }
  }
}

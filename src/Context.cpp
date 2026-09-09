#include "Context.h"
#include "Globals.h"
#include "Utils.h"
#include <algorithm>
#include <fstream>
#include <utility>
#include <vector>
#include <kenshi/Building/Building.h>
#include <kenshi/CharStats.h>
#include <kenshi/Character.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/InstanceID.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/ZoneManager.h>
#include <kenshi/Platoon.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/RaceData.h>
#include <kenshi/util/hand.h>
#include <sstream>

// Sort helper for the nearby scan. Kept as a struct rather than a lambda:
// the mod is built with the Windows 7.1 SDK toolset, where C++11 lambdas
// are not guaranteed to compile.
struct NearbyCloserFirst {
  bool operator()(const std::pair<float, Character *> &a,
                  const std::pair<float, Character *> &b) const {
    return a.first < b.first;
  }
};

std::string SlotToString(AttachSlot slot) {
  switch (slot) {
  case ATTACH_WEAPON:
    return "weapon";
  case ATTACH_BACK:
    return "back";
  case ATTACH_HAIR:
    return "hair";
  case ATTACH_HAT:
    return "hat";
  case ATTACH_EYES:
    return "eyes";
  case ATTACH_BODY:
    return "body";
  case ATTACH_LEGS:
    return "legs";
  case ATTACH_SHIRT:
    return "shirt";
  case ATTACH_BOOTS:
    return "boots";
  case ATTACH_GLOVES:
    return "gloves";
  case ATTACH_NECK:
    return "neck";
  case ATTACH_BACKPACK:
    return "backpack";
  case ATTACH_BEARD:
    return "beard";
  case ATTACH_BELT:
    return "belt";
  case ATTACH_LEFT_ARM:
    return "left_arm";
  case ATTACH_RIGHT_ARM:
    return "right_arm";
  case ATTACH_LEFT_LEG:
    return "left_leg";
  case ATTACH_RIGHT_LEG:
    return "right_leg";
  default:
    return "none";
  }
}

void GetAllCharacterItems(Character *npc, std::vector<Item *> &outItems) {
  if (!npc)
    return;
  Inventory *inv = npc->getInventory();
  if (!inv)
    return;

  lektor<InventorySection *> &sections = inv->sectionsInSearchOrder;
  for (uint32_t s = 0; s < sections.size(); ++s) {
    InventorySection *sect = sections[s];
    if (sect) {
      const Ogre::vector<InventorySection::SectionItem>::type &items =
          sect->getItems();
      for (uint32_t i = 0; i < items.size(); ++i) {
        if (items[i].item)
          outItems.push_back(items[i].item);
      }
    }
  }

  ContainerItem *backpack = npc->hasABackpackOn();
  if (backpack && backpack->inventory) {
    lektor<InventorySection *> &bpSections =
        backpack->inventory->sectionsInSearchOrder;
    for (uint32_t s = 0; s < bpSections.size(); ++s) {
      InventorySection *sect = bpSections[s];
      if (sect) {
        const Ogre::vector<InventorySection::SectionItem>::type &items =
            sect->getItems();
        for (uint32_t i = 0; i < items.size(); ++i) {
          if (items[i].item)
            outItems.push_back(items[i].item);
        }
      }
    }
  }
}

// ── Оценка боевой силы ───────────────────────────────────────────────────────
// NPC прикидывает, справится ли его сторона с игроком и его людьми. Считаем
// здесь, в игре: у сервера нет параметров соседей, а слать их пачкой в каждый
// запрос дорого. Наружу уходит только короткий вывод.

// Доля уцелевшего здоровья: избитый боец стоит меньше целого.
static float SS_HealthFraction(Character *c) {
  if (!c || (uintptr_t)c < 0x1000)
    return 0.0f;
  MedicalSystem *med = c->getMedical();
  if (!med || (uintptr_t)med < 0x1000)
    return 0.0f;
  if (med->dead || med->unconcious)
    return 0.0f; // лежачий в счёт не идёт
  MedicalSystem::HealthPartStatus *parts[6] = {med->getPart(0), med->getPart(1),
                                               med->leftArm,    med->rightArm,
                                               med->leftLeg,    med->rightLeg};
  float sum = 0.0f;
  int counted = 0;
  for (int i = 0; i < 6; ++i) {
    MedicalSystem::HealthPartStatus *p = parts[i];
    if (!p)
      continue;
    float mx = p->maxHealth();
    if (mx <= 0.0f)
      continue;
    float frac = p->flesh / mx;
    if (frac < 0.0f)
      frac = 0.0f;
    if (frac > 1.0f)
      frac = 1.0f;
    sum += frac;
    ++counted;
  }
  if (!counted)
    return 1.0f;
  return sum / (float)counted;
}

// Боевая ценность одного человека. Атака и защита — главное: это прямое
// умение драться. Стойкость весит много, потому что живучий выигрывает
// затяжную схватку. Сила и ловкость дают урон и темп, но сами бой не решают.
static float SS_CombatPower(Character *c) {
  if (!c || (uintptr_t)c < 0x1000)
    return 0.0f;
  CharStats *st = NULL;
  try {
    st = c->getStats();
  } catch (...) {
    return 0.0f;
  }
  if (!st || (uintptr_t)st < 0x1000)
    return 0.0f;
  float power = 0.0f;
  try {
    power = st->getStat(STAT_MELEE_ATTACK, false) +
            st->getStat(STAT_MELEE_DEFENCE, false) +
            (float)st->_strength * 0.5f +
            (float)st->_toughness * 0.75f +
            (float)st->_dexterity * 0.5f;
  } catch (...) {
    return 0.0f;
  }
  if (power < 0.0f)
    power = 0.0f;
  return power * SS_HealthFraction(c);
}

static std::string GetHealthStatus(Character *npc) {
  if (!npc || (uintptr_t)npc < 0x1000)
    return "Unknown";
  MedicalSystem *med = npc->getMedical();
  if (!med || (uintptr_t)med < 0x1000)
    return "Unknown";

  if (med->dead)
    return "Dead";
  if (med->unconcious)
    return "Unconscious";
  if (npc->_currentProneState == PS_PLAYING_DEAD)
    return "Playing Dead";

  bool crippled = false;
  if (med->leftLeg && med->leftLeg->flesh < 0)
    crippled = true;
  if (med->rightLeg && med->rightLeg->flesh < 0)
    crippled = true;

  bool injured = false;
  // Check major parts
  MedicalSystem::HealthPartStatus *parts[6] = {med->getPart(0), med->getPart(1),
                                               med->leftArm,    med->rightArm,
                                               med->leftLeg,    med->rightLeg};
  for (int i = 0; i < 6; ++i) {
    MedicalSystem::HealthPartStatus *p = parts[i];
    if (p && p->flesh < p->maxHealth() * 0.70f)
      injured = true;
  }

  if (crippled)
    return "Crippled";
  if (injured)
    return "Injured";
  return "Healthy";
}

static std::string GetVisibleEquipment(Character *npc) {
  if (!npc || (uintptr_t)npc < 0x1000)
    return "";
  Inventory *inv = npc->getInventory();
  if (!inv || (uintptr_t)inv < 0x1000)
    return "";

  std::string eq = "";
  lektor<Item *> armor;
  inv->getEquippedArmour(armor);
  for (uint32_t i = 0; i < armor.size(); ++i) {
    if (armor.stuff[i] && (uintptr_t)armor.stuff[i] > 0x1000) {
      if (!eq.empty())
        eq += ", ";
      eq += armor.stuff[i]->getName();
    }
  }

  lektor<Item *> weapons;
  inv->getEquippedWeapons(weapons);
  for (uint32_t i = 0; i < weapons.size(); ++i) {
    if (weapons.stuff[i] && (uintptr_t)weapons.stuff[i] > 0x1000) {
      if (!eq.empty())
        eq += ", ";
      eq += weapons.stuff[i]->getName();
    }
  }
  return eq;
}

std::string GetStorageIDFor(Character *npc, const std::string &name,
                            const std::string &factionName) {
  // PERSISTENCE UPGRADE: Use name-only storage IDs.
  // This solves the "Faction Change" problem and provides a cleaner filesystem.
  return name;
}

std::string GetIdentityFaction(Character *npc) {
  if (!npc || (uintptr_t)npc < 0x1000)
    return "Neutral";

  Faction *faction = nullptr;
  try {
    faction = npc->getFaction() ? npc->getFaction() : npc->owner;
  } catch (...) {
  }

  std::string factionName = "Neutral";
  if (faction && (uintptr_t)faction > 0x1000) {
    factionName = faction->getName();
    if (factionName.empty() || factionName == "Unknown") {
      if (faction->data && !faction->data->name.empty())
        factionName = faction->data->name;
    }
  }

  std::string identityFaction = factionName;
  unsigned int serial = npc->getHandle().serial;

  std::string cached = "";
  EnterCriticalSection(&g_stateMutex);
  if (g_originFactions.count(serial)) {
    cached = g_originFactions[serial];
  }
  LeaveCriticalSection(&g_stateMutex);

  if (!cached.empty()) {
    return cached;
  }

  // If they are in the player squad, try to find their true origin
  if (faction && faction->isThePlayer()) {
    GameData *characterData = npc->getGameData();
    if (characterData && ppWorld && *ppWorld && (*ppWorld)->factionMgr) {
      const Ogre::vector<GameDataReference>::type *refs =
          characterData->getReferenceListIfExists("faction");
      if (refs && !refs->empty()) {
        Faction *refFaction =
            (*ppWorld)->factionMgr->getFactionByStringID(refs->at(0).sid);
        if (refFaction && !refFaction->isThePlayer()) {
          identityFaction = refFaction->getName();
          g_originFactions[serial] = identityFaction;
          return identityFaction;
        }
      }
    }
    // If we still didn't find an origin, use a generic label to avoid
    // volatile player faction names (which change if the player renames their
    // squad)
    if (identityFaction == factionName || identityFaction == "Nameless") {
      identityFaction = "Drifters";
    }
  } else {
    // For non-player NPCs, their current faction is their stable identity
    if (!factionName.empty() && factionName != "Unknown" &&
        factionName != "Neutral") {
      EnterCriticalSection(&g_stateMutex);
      g_originFactions[serial] = factionName;
      LeaveCriticalSection(&g_stateMutex);
    }
  }
  return identityFaction;
}

std::string GetDetailedContext(Character *npc, const std::string &type) {
  if (!npc || (uintptr_t)npc < 0x1000 || !ppWorld || !(*ppWorld))
    return "{}";

  std::string json = "{";
  // Write 'type' first so Python can route player vs NPC contexts correctly
  json += "\"type\": \"" + type + "\",";
  if (ppWorld && *ppWorld) {
    TimeOfDay tod = (*ppWorld)->getTimeStamp_inGameHours();
    // Kenshi Time tracking:
    // getTotalDays() usually matches game clock days
    // we use total hours/minutes for the remainder of the clock
    int day = (int)tod.getTotalDays();
    int hour = (int)fmod(tod.getTotalHours(), 24.0);
    int minute = (int)fmod(tod.getTotalMinutes(), 60.0);

    json += "\"day\": " + ToString(day) + ",";
    json += "\"hour\": " + ToString(hour) + ",";
    json += "\"minute\": " + ToString(minute) + ",";
    json +=
        "\"gamespeed\": " + ToString((*ppWorld)->getFrameSpeedMultiplier()) +
        ",";
    json += "\"is_paused\": " +
            std::string((*ppWorld)->isPaused() ? "true" : "false") + ",";

    // AI Timers for Debugger
    DWORD now = GetTickCount();
    DWORD elapsed = now - g_lastAmbientTick;
    json += "\"radiant_timer_ms\": " + ToString((int)elapsed) + ",";
    json += "\"radiant_interval_ms\": " +
            ToString((int)(g_ambientIntervalSeconds * 1000)) + ",";

    DWORD speech_elapsed = now - g_lastDialogueTick;
    json += "\"speech_delay_ms\": " + ToString((int)speech_elapsed) + ",";
    json += "\"speech_interval_ms\": " +
            ToString((int)(g_dialogueSpeedSeconds * 1000)) + ",";
  }

  // --- Character State (before name, so Python can gate early on dead/KO) ---
  std::string charState = "normal";
  bool isDead = false;
  bool isUnconcious = false;
  try {
    isDead = npc->isDead();
    if (isDead) {
      charState = "dead";
    } else {
      isUnconcious = npc->isUnconcious();
      if (isUnconcious) {
        charState = "unconscious";
      } else if (npc->inSomething == IN_PRISON) {
        charState = "imprisoned";
      } else {
        // Enslaved: currently assigned as a slave
        try {
          SlaveStateEnum slaveState = npc->isSlave();
          bool chained = npc->isChainedMode();
          if (slaveState != 0) { // 0 == not a slave
            // Escaped slave: has slave status but no chains/owner
            charState = chained ? "enslaved" : "escaped-slave";
          }
        } catch (...) {
        }
      }
    }
  } catch (...) {
  }
  json += "\"character_state\": \"" + charState + "\",";
  json += "\"is_incapacitated\": " +
          std::string((isDead || isUnconcious) ? "true" : "false") + ",";

  std::string name = "Unknown";
  try {
    name = npc->getName();
    if (name.empty() || name == "Unknown Entity" || name == "Unknown") {
      if (!npc->displayName.empty())
        name = npc->displayName;
      else if (npc->data && !npc->data->name.empty())
        name = npc->data->name;
    }
  } catch (...) {
  }
  json += "\"name\": \"" + EscapeJSON(name) + "\",";

  InstanceID *iid = npc->getInstanceID();
  if (iid && !iid->uid.empty()) {
    json += "\"id\": \"" + EscapeJSON(iid->uid) + "\",";
  } else {
    json += "\"id\": \"hand_" + ToString((int)npc->getHandle().serial) + "\",";
  }

  // Robust Race Name
  RaceData *race = nullptr;
  try {
    race = npc->getRace() ? npc->getRace() : npc->myRace;
  } catch (...) {
  }

  std::string raceName = "Unknown";
  if (race && (uintptr_t)race > 0x1000) {
    if (race->data && !race->data->name.empty())
      raceName = race->data->name;
    else if (race->data && !race->data->stringID.empty())
      raceName = race->data->stringID;
  }
  json += "\"race\": \"" + EscapeJSON(raceName) + "\",";

  // Robust Gender
  std::string gender = "male";
  try {
    gender = npc->isFemale() ? "female" : "male";
  } catch (...) {
  }
  if (npc->sex == "female" || npc->sex == "male")
    gender = npc->sex;
  json += "\"gender\": \"" + gender + "\",";

  // Robust Faction Name
  Faction *faction = nullptr;
  try {
    faction = npc->getFaction() ? npc->getFaction() : npc->owner;
  } catch (...) {
  }

  std::string factionName = "Neutral";
  std::string factionID = "Neutral";
  if (faction && (uintptr_t)faction > 0x1000) {
    std::string fn = faction->getName();
    if (!fn.empty() && fn != "Unknown")
      factionName = fn;
    else if (faction->data && !faction->data->name.empty())
      factionName = faction->data->name;

    if (faction->data && !faction->data->stringID.empty())
      factionID = faction->data->stringID;
    else
      factionID = factionName;
  }
  json += "\"faction\": \"" + EscapeJSON(factionName) + "\",";
  json += "\"factionID\": \"" + EscapeJSON(factionID) + "\",";

  // Job / Assigned Tasks
  std::string job = "None";
  try {
    int jobCount = npc->getPermajobCount();
    if (jobCount > 0) {
      job = "";
      for (int i = 0; i < jobCount; ++i) {
        std::string jName = npc->getPermajobName(i);
        if (!jName.empty()) {
          if (!job.empty())
            job += ", ";
          job += jName;
        }
      }
      if (job.empty())
        job = "None";
    }
  } catch (...) {
  }
  json += "\"job\": \"" + EscapeJSON(job) + "\",";

  // IDENTITY STABILITY
  std::string identityFaction = GetIdentityFaction(npc);
  json += "\"origin_faction\": \"" + EscapeJSON(identityFaction) + "\",";

  // Stable Storage ID: Prioritize InstanceID (UUID) or the stable Identity
  // Faction.
  std::string stableID = GetStorageIDFor(npc, name, identityFaction);
  json += "\"storage_id\": \"" + EscapeJSON(stableID) + "\",";

  // Relation to Player Faction
  if (ppWorld && *ppWorld && (*ppWorld)->player &&
      (*ppWorld)->player->getFaction() && faction) {
    Faction *playerFaction = (*ppWorld)->player->getFaction();
    if (playerFaction->relations) {
      float rel = playerFaction->relations->getFactionRelation(faction);
      json += "\"relation\": " + ToString((int)rel) + ",";
    }
  }

  bool isTrader = false;
  try {
    isTrader = npc->isATrader();
  } catch (...) {
  }
  json += "\"is_trader\": " + std::string(isTrader ? "true" : "false") + ",";

  bool isLeader = false;
  if (faction && (uintptr_t)faction > 0x1000 && faction->data &&
      (uintptr_t)faction->data > 0x1000) {
    hand lHand;
    if (faction->data->getHandle(lHand, "leader") && lHand.isValid()) {
      if (lHand == npc->getHandle())
        isLeader = true;
    }
  }
  json += "\"is_leader\": " + std::string(isLeader ? "true" : "false") + ",";

  // Building Context
  bool indoors = false;
  std::string buildingName = "Unknown";
  bool inAShop = false;
  const hand &buildingHandle = npc->isIndoors();
  if (buildingHandle.isValid()) {
    indoors = true;
    Building *b = buildingHandle.getBuilding();
    if (b) {
      buildingName = b->getName();
      if (b->isAShop() || b->designation == BD_SHOP ||
          b->designation == BD_BAR) {
        inAShop = true;
      }
    }
  }

  json += "\"indoors\": " + std::string(indoors ? "true" : "false") + ",";
  json += "\"in_shop\": " + std::string(inAShop ? "true" : "false") + ",";
  json += "\"building_name\": \"" + EscapeJSON(buildingName) + "\",";

  if (ppWorld && *ppWorld) {
    lektor<RootObject *> results;
    // The sixth argument caps how many characters the engine returns, and
    // the result is NOT ordered by distance. With a cap of 16 the caller
    // could receive a crowd standing 80m away while missing the caged
    // slaves two metres off. Ask for a large set, then keep the nearest.
    (*ppWorld)->getCharactersWithinSphere(
        results, npc->getPosition(), g_visionRange, 0.0f, 0.0f, 100, 0, npc);

    std::vector<std::pair<float, Character *> > sortedNearby;
    for (uint32_t si = 0; si < results.size(); ++si) {
      Character *cand = (Character *)results.stuff[si];
      if (!cand || (uintptr_t)cand < 0x1000)
        continue;
      sortedNearby.push_back(std::make_pair(
          npc->getPosition().distance(cand->getPosition()), cand));
    }
    std::sort(sortedNearby.begin(), sortedNearby.end(),
              NearbyCloserFirst());
    const size_t kNearbyLimit = 20;
    if (sortedNearby.size() > kNearbyLimit)
      sortedNearby.resize(kNearbyLimit);
    Log("CONTEXT_NEARBY: " + ToString((int)results.size()) +
        " found, sending " + ToString((int)sortedNearby.size()) + " nearest");

    json += "\"nearby\": [";
    for (uint32_t i = 0; i < sortedNearby.size(); ++i) {
      Character *other = sortedNearby[i].second;
      if (other && (uintptr_t)other > 0x1000) {
        // ONLY exclude the primary player character (typically the first char
        // in first squad)
        if ((*ppWorld)->player &&
            (*ppWorld)->player->playerCharacters.size() > 0) {
          if (other == (*ppWorld)->player->playerCharacters[0])
            continue;
        }

        if (json.back() == '}')
          json += ",";

        std::string o_name = other->getName();

        // Robust Race Name
        RaceData *o_race = other->getRace() ? other->getRace() : other->myRace;
        std::string o_rn = "Unknown";
        if (o_race && (uintptr_t)o_race > 0x1000) {
          if (o_race->data && !o_race->data->name.empty())
            o_rn = o_race->data->name;
          else if (o_race->data && !o_race->data->stringID.empty())
            o_rn = o_race->data->stringID;
        }

        // Robust Faction Name
        Faction *o_fact =
            other->getFaction() ? other->getFaction() : other->owner;
        std::string o_fn = "Neutral";
        if (o_fact && (uintptr_t)o_fact > 0x1000) {
          std::string fn = o_fact->getName();
          if (!fn.empty() && fn != "Unknown")
            o_fn = fn;
          else if (o_fact->data && !o_fact->data->name.empty())
            o_fn = o_fact->data->name;
          else if (o_fact->data && !o_fact->data->stringID.empty())
            o_fn = o_fact->data->stringID;
        }

        std::string o_gender = other->isFemale() ? "female" : "male";
        float dist = npc->getPosition().distance(other->getPosition());

        // IDENTITY STABILITY: Use the origin-faction cache for overhearers too!
        std::string o_sid_fact = o_fn;
        unsigned int o_serial = other->getHandle().serial;

        EnterCriticalSection(&g_stateMutex);
        if (g_originFactions.count(o_serial)) {
          o_sid_fact = g_originFactions[o_serial];
        } else if (o_fact && !o_fact->isThePlayer()) {
          // For non-player characters, cache their current faction as origin
          g_originFactions[o_serial] = o_fn;
          o_sid_fact = o_fn;
        }
        LeaveCriticalSection(&g_stateMutex);

        // Include storage_id for perfect overhearer-to-participant mapping
        std::string o_sid = GetStorageIDFor(other, o_name, o_sid_fact);

        // Sensory details for "looking" around
        std::string o_health = GetHealthStatus(other);
        std::string o_equip = GetVisibleEquipment(other);

        json += "{\"name\":\"" + EscapeJSON(o_name) + "\",";
        json += "\"race\":\"" + EscapeJSON(o_rn) + "\",";
        json += "\"faction\":\"" + EscapeJSON(o_fn) + "\",";
        json += "\"gender\":\"" + EscapeJSON(o_gender) + "\",";
        json += "\"health\":\"" + EscapeJSON(o_health) + "\",";
        json += "\"equipment\":\"" + EscapeJSON(o_equip) + "\",";
        json += "\"storage_id\":\"" + EscapeJSON(o_sid) + "\",";
        // Same state the primary NPC reports, so a slaver can see which
        // of the people around him are caged or in chains, and the server
        // can build a price list without guessing from names.
        std::string o_state = "normal";
        try {
          if (other->isDead()) {
            o_state = "dead";
          } else if (other->isUnconcious()) {
            o_state = "unconscious";
          } else if (other->inSomething == IN_PRISON) {
            o_state = "imprisoned";
          } else {
            SlaveStateEnum o_slave = other->isSlave();
            if (o_slave != 0)
              o_state = other->isChainedMode() ? "enslaved" : "escaped-slave";
          }
        } catch (...) {
        }
        json += "\"character_state\":\"" + o_state + "\",";
        json += "\"dist\":" + ToString(dist) + "}";
      }
    }
    json += "],";

    // ── Расклад сил ─────────────────────────────────────────────────────────
    // Складываем боевую ценность обеих сторон: этот персонаж со своими
    // соратниками против игрока с его людьми. Берём только тех, кто РЯДОМ:
    // отряд, оставленный в другом городе, в драке не поможет.
    {
      const float kAssessRadius = 100.0f;
      float ownPower = SS_CombatPower(npc);
      float foePower = 0.0f;
      int ownCount = 1;
      int foeCount = 0;

      Faction *myFac = NULL;
      try {
        myFac = npc->getFaction() ? npc->getFaction() : npc->owner;
      } catch (...) {
      }

      std::vector<Character *> playerSide;
      if ((*ppWorld)->player) {
        for (uint32_t pi = 0;
             pi < (*ppWorld)->player->playerCharacters.size(); ++pi) {
          Character *pc = (*ppWorld)->player->playerCharacters[pi];
          if (!pc || (uintptr_t)pc < 0x1000)
            continue;
          playerSide.push_back(pc);
          float pd = npc->getPosition().distance(pc->getPosition());
          if (pd > kAssessRadius)
            continue;
          float pp = SS_CombatPower(pc);
          if (pp > 0.0f) {
            foePower += pp;
            ++foeCount;
          }
        }
      }

      for (uint32_t ai = 0; ai < results.size(); ++ai) {
        Character *ally = (Character *)results.stuff[ai];
        if (!ally || (uintptr_t)ally < 0x1000 || ally == npc)
          continue;
        bool isPlayerSide = false;
        for (uint32_t pj = 0; pj < playerSide.size(); ++pj) {
          if (playerSide[pj] == ally) {
            isPlayerSide = true;
            break;
          }
        }
        if (isPlayerSide)
          continue;
        float ad = npc->getPosition().distance(ally->getPosition());
        if (ad > kAssessRadius)
          continue;
        Faction *af = NULL;
        try {
          af = ally->getFaction() ? ally->getFaction() : ally->owner;
        } catch (...) {
        }
        if (!myFac || af != myFac)
          continue;
        float ap = SS_CombatPower(ally);
        if (ap > 0.0f) {
          ownPower += ap;
          ++ownCount;
        }
      }

      // Три ступени: заметно слабее — боится, примерно поровну — настороже,
      // заметно сильнее — не боится. Полуторный перевес считаем заметным.
      //
      // Если собственная сила нулевая, оценивать нечего: персонаж либо лежит
      // без сознания, либо игра ещё не отдала о нём данных. Раньше в обоих
      // случаях выходило «поровну», что неверно, — теперь честное «неизвестно»,
      // чтобы сервер не строил поведение на пустоте.
      const char *verdict = "unknown";
      if (ownPower > 0.0f) {
        verdict = "even";
        if (foePower > ownPower * 1.5f)
          verdict = "outmatched";
        else if (foePower * 1.5f < ownPower)
          verdict = "dominant";
      }

      json += "\"threat_balance\": {";
      json += "\"verdict\":\"" + std::string(verdict) + "\",";
      json += "\"own_count\":" + ToString(ownCount) + ",";
      json += "\"foe_count\":" + ToString(foeCount) + "},";
      Log("THREAT_BALANCE: " + npc->getName() + " own=" + ToString((int)ownPower) +
          "(" + ToString(ownCount) + ") foe=" + ToString((int)foePower) + "(" +
          ToString(foeCount) + ") -> " + std::string(verdict));
    }
  }

  int money = npc->getMoney();
  if (money <= 0 && npc->getOwnerships())
    money = npc->getOwnerships()->getMoney();
  json += "\"money\": " + ToString(money) + ",";

  if (type == "player" && ppWorld && *ppWorld && (*ppWorld)->player) {
    json += "\"squad\": [";
    for (uint32_t i = 0; i < (*ppWorld)->player->playerCharacters.size(); ++i) {
      if (i > 0)
        json += ",";
      json += "\"" +
              EscapeJSON((*ppWorld)->player->playerCharacters[i]->getName()) +
              "\"";
    }
    json += "],";
  }

  CharStats *stats = npc->getStats();
  if (stats) {
    json += "\"stats\": {";
    json += "\"strength\": " + ToString((int)stats->_strength) + ",";
    json += "\"dexterity\": " + ToString((int)stats->_dexterity) + ",";
    json += "\"toughness\": " + ToString((int)stats->_toughness) + ",";
    json += "\"perception\": " + ToString((int)stats->perception) + ",";
    json += "\"melee_attack\": " +
            ToString((int)stats->getStat(STAT_MELEE_ATTACK, false)) + ",";
    json += "\"melee_defence\": " +
            ToString((int)stats->getStat(STAT_MELEE_DEFENCE, false)) + ",";
    json += "\"athletics\": " +
            ToString((int)stats->getStat(STAT_ATHLETICS, false));
    json += "},";
  }

  MedicalSystem *med = npc->getMedical();
  if (med) {
    json += "\"medical\": {";
    json += "\"blood\": " + ToString((int)med->blood) + ",";
    json += "\"max_blood\": " + ToString((int)med->getMaxBlood()) + ",";
    json += "\"blood_rate\": " + ToString(med->currentBleedRate) + ",";
    // Hunger is stored as deficit (0 when full, 300 when starving)
    // We add 'fed' to account for food currently being digested.
    float hungerVal = (300.0f - med->hunger) + med->fed;
    if (hungerVal < 0)
      hungerVal = 0;
    json += "\"hunger\": " + ToString((int)hungerVal) + ",";
    json += "\"is_unconscious\": " +
            std::string(med->unconcious ? "true" : "false") + ",";
    json += "\"limbs\": {";
    auto addPart = [&](const std::string &name,
                       MedicalSystem::HealthPartStatus *p) {
      json += "\"" + name + "\": " + ToString(p ? (int)p->flesh : 100) + ",";
      json += "\"" + name +
              "_max\": " + ToString(p ? (int)p->maxHealth() : 100) + ",";
      json += "\"" + name +
              "_robotic\": " +
              std::string((p && p->isRobotic()) ? "true" : "false") + ",";
    };
    addPart("head", med->getPart(0));
    addPart("stomach", med->getPart(1));
    addPart("left_arm", med->leftArm);
    addPart("right_arm", med->rightArm);
    addPart("left_leg", med->leftLeg);
    addPart("right_leg", med->rightLeg);
    if (json.back() == ',')
      json.pop_back();
    json += "}";
    json += "},";
  }

  json += "\"environment\": {";
  json += "\"indoors\": " +
          std::string(npc->isIndoors().isValid() ? "true" : "false") + ",";
  json += "\"in_town\": " +
          std::string(npc->amInsideTownWalls() ? "true" : "false") + ",";
  TownBase *town = npc->getCurrentTownLocation();
  if (town)
    json += "\"town_name\": \"" +
            EscapeJSON(((RootObjectBase *)town)->getName()) + "\",";
  // --- REGION (world zone) so the NPC knows where in the world it stands ---
  if (ppWorld && *ppWorld && (*ppWorld)->zoneMgr) {
    ZoneMap *ssZone = (*ppWorld)->zoneMgr->getZoneMap(npc->getPosition());
    if (ssZone && (uintptr_t)ssZone > 0x1000 && ssZone->getBiomeCount() > 0) {
      GameData *ssBiome = ssZone->getBiomeData(0);
      if (ssBiome && (uintptr_t)ssBiome > 0x1000 && !ssBiome->name.empty())
        json += "\"region\": \"" + EscapeJSON(ssBiome->name) + "\",";
    }
  }
  json += "\"weather\": " + ToString((int)npc->getCurrentWeatherAffectStatus());
  json += "},";

  json += "\"inventory\": ";
  if (GetCurrentThreadId() == g_mainThreadId) {
    json += "[";
    std::vector<Item *> allItems;
    GetAllCharacterItems(npc, allItems);
    for (uint32_t i = 0; i < allItems.size(); ++i) {
      if (allItems[i]) {
        if (i > 0)
          json += ",";
        
        // Get the real price using the engine's internal valuation logic
        int price = 0;
        try {
            price = allItems[i]->getValueSingle(false);
        } catch (...) {
            price = 0;
        }

        json +=
            "{\"name\": \"" + EscapeJSON(allItems[i]->getName()) +
            "\", \"count\": " + ToString((int)allItems[i]->quantity) +
            ", \"price\": " + ToString(price) +
            ", \"equipped\": " + (allItems[i]->isEquipped ? "true" : "false") +
            ", \"slot\": \"" + SlotToString(allItems[i]->slotType) + "\"}";
      }
    }
    json += "],";
  } else if (npc->getHandle() == g_lastInventoryHand) {
    json += g_activeInventoryJson + ",";
  } else if (type == "player" && !g_playerInventoryJson.empty()) {
    json += g_playerInventoryJson + ",";
  } else {
    json += "[],";
  }

  json += "\"events\": [";
  EnterCriticalSection(&g_eventMutex);
  int eventCount = 0;
  for (int i = (int)g_gameEvents.size() - 1; i >= 0 && eventCount < 30;
       --i, ++eventCount) {
    if (eventCount > 0)
      json += ",";
    json += "{\"type\": \"" + EscapeJSON(g_gameEvents[i].type) + "\",";
    json += "\"actor\": \"" + EscapeJSON(g_gameEvents[i].actor) + "\",";
    json += "\"actor_faction\": \"" + EscapeJSON(g_gameEvents[i].actorFaction) +
            "\",";
    json += "\"target\": \"" + EscapeJSON(g_gameEvents[i].target) + "\",";
    json += "\"target_faction\": \"" +
            EscapeJSON(g_gameEvents[i].targetFaction) + "\",";
    json += "\"msg\": \"" + EscapeJSON(g_gameEvents[i].message) + "\",";
    json += "\"age\": " +
            ToString((int)(GetTickCount() - g_gameEvents[i].timestamp) / 1000) +
            "}";
  }
  LeaveCriticalSection(&g_eventMutex);
  json += "],";

  if (ppWorld && *ppWorld && (*ppWorld)->player &&
      (*ppWorld)->player->playerCharacters.size() > 0) {
    Character *player = (*ppWorld)->player->playerCharacters[0];
    json += "\"memories\": { \"short_term\": [";
    bool first = true;
    for (int i = 1; i < 8; ++i) {
      if (npc->getCharacterMemoryTag(player,
                                     (CharacterPerceptionTags_ShortTerm)i)) {
        if (!first)
          json += ",";
        json += ToString(i);
        first = false;
      }
    }
    json += "], \"long_term\": [";
    first = true;
    for (int i = 1; i < 17; ++i) {
      if (npc->getCharacterMemoryTag(player,
                                     (CharacterPerceptionTags_LongTerm)i)) {
        if (!first)
          json += ",";
        json += ToString(i);
        first = false;
      }
    }
    json += "] }";
  } else {
    json += "\"memories\": {}";
  }

  json += "}";
  return json;
}

std::string GetWorldEventsContext() {
  std::ifstream file("mods/SentientSands/world_events.txt");
  if (file.is_open()) {
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return EscapeJSON(content);
  }
  return "No major world events reported.";
}

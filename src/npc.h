// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_NPC_H
#define FS_NPC_H

#include "creature.h"
#include "luascript.h"

#include <map>
#include <set>

class Npc;
class Player;

struct NpcTypeInfo {
	LuaScriptInterface* scriptInterface = nullptr;
	NpcsEvent_t eventType = NPCS_EVENT_NONE;

	// Event IDs
	int32_t thinkEvent = -1;
	int32_t creatureAppearEvent = -1;
	int32_t creatureDisappearEvent = -1;
	int32_t creatureMoveEvent = -1;
	int32_t creatureSayEvent = -1;
	int32_t playerBuyEvent = -1;
	int32_t playerSellEvent = -1;
	int32_t playerCheckEvent = -1;
	int32_t playerCloseChannelEvent = -1;

	// Properties
	Outfit_t outfit;
	int32_t health = 100;
	int32_t healthMax = 100;
	uint32_t baseSpeed = 100;
	uint32_t walkInterval = 1500;
	int32_t walkRadius = 2;
	uint8_t speechBubble = 0;
	uint16_t moneyType = 0;
	bool pushable = true;
	bool attackable = false;
	bool floorChange = false;
	bool ignoreHeight = false;

	// Shop
	std::vector<ShopInfo> shopItems;
};

class NpcType {
public:
	NpcType() = default;
	NpcType(const NpcType&) = delete;
	NpcType& operator=(const NpcType&) = delete;

	bool loadCallback(LuaScriptInterface* scriptInterface);

	std::string name;
	std::string nameDescription;
	NpcTypeInfo info;
};

class Npcs
{
public:
	static void reload();
	static NpcType* getNpcType(const std::string& name, bool create = false);
	static std::map<std::string, NpcType> npcTypes;
};

class Npc final : public Creature
{
public:
	~Npc();

	using Creature::onWalk;

	// non-copyable
	Npc(const Npc&) = delete;
	Npc& operator=(const Npc&) = delete;

	Npc* getNpc() override { return this; }
	const Npc* getNpc() const override { return this; }

	bool isPushable() const override { return pushable && walkTicks != 0; }

	void setID() override
	{
		if (id == 0) {
			id = ++npcAutoID;
		}
	}

	void removeList() override;
	void addList() override;

	static std::unique_ptr<Npc> createNpc(const std::string &name);

	bool canSee(const Position& pos) const override;

	bool load();
	void reload();

	const std::string& getName() const override { return name; }
	const std::string& getNameDescription() const override { return name; }

	CreatureType_t getType() const override { return CREATURETYPE_NPC; }

	uint16_t getMoneyType() const { return moneyType; }

	void doSay(std::string_view text);
	void doSayToPlayer(Player* player, std::string_view text);

	bool doMoveTo(const Position& pos, int32_t minTargetDist = 1, int32_t maxTargetDist = 1, bool fullPathSearch = true,
	              bool clearSight = true, int32_t maxSearchDist = 0);

	int32_t getMasterRadius() const { return masterRadius; }
	const Position& getMasterPos() const { return masterPos; }
	void setMasterPos(Position pos, int32_t radius = 1)
	{
		masterPos = pos;
		if (masterRadius == -1) {
			masterRadius = radius;
		}
	}

	void onPlayerCloseChannel(Player* player);
	void onPlayerTrade(Player* player, int32_t callback, uint16_t itemId, uint8_t count, uint8_t amount,
	                   bool ignore = false, bool inBackpacks = false);
	void onPlayerEndTrade(Player* player, int32_t buyCallback, int32_t sellCallback);

	void turnToCreature(Creature* creature);
	void setCreatureFocus(Creature* creature);

	const auto& getSpectators() { return spectators; }

	// RevScriptSys player interaction tracking
	void setPlayerInteraction(uint32_t playerId, int32_t topic = 0);
	bool isInteractingWithPlayer(uint32_t playerId) const;
	void removePlayerInteraction(uint32_t playerId);
	bool isMerchant() const { return npcType != nullptr && !npcType->info.shopItems.empty(); }

	// RevScriptSys public accessors
	NpcType* getNpcType() const { return npcType; }
	void openShopForPlayer(Player* player) { addShopPlayer(player); }
	void closeShopForPlayer(Player* player) { removeShopPlayer(player); }
	LuaScriptInterface* getShopScriptInterface() const {
		return npcType ? npcType->info.scriptInterface : nullptr;
	}

	static uint32_t npcAutoID;

	explicit Npc(const std::string& name);

private:
	void onCreatureAppear(Creature* creature, bool isLogin) override;
	void onRemoveCreature(Creature* creature, bool isLogout) override;
	void onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile,
	                    const Position& oldPos, bool teleport) override;

	void onCreatureSay(Creature* creature, SpeakClasses type, std::string_view text) override;
	void onThink(uint32_t interval) override;
	std::string getDescription(int32_t lookDistance) const override;

	bool isImmune(CombatType_t) const override { return !attackable; }
	bool isImmune(ConditionType_t) const override { return !attackable; }
	bool isAttackable() const override { return attackable; }
	bool getNextStep(Direction& dir, uint32_t& flags) override;

	void setIdle(const bool idle);

	bool canWalkTo(const Position& fromPos, Direction dir) const;
	bool getRandomStep(Direction& direction) const;

	void reset();
	bool loadFromLuaType();

	void addShopPlayer(Player* player);
	void removeShopPlayer(Player* player);
	void closeAllShopWindows();

	std::map<std::string, std::string> parameters;

	std::set<Player*> shopPlayerSet;
	std::set<Player*> spectators;

	std::string name;
	std::string filename;

	NpcType* npcType = nullptr;
	std::map<uint32_t, int32_t> playerInteractions;

	Position masterPos;

	uint32_t walkTicks;
	int32_t focusCreature;
	int32_t masterRadius;

	uint8_t speechBubble;

	bool floorChange;
	bool attackable;
	bool ignoreHeight;
	bool loaded;
	bool isIdle;
	bool pushable;

	uint16_t moneyType;

	friend class Npcs;
};

#endif

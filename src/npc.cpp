// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "npc.h"

#include "game.h"
#include "logger.h"
#include "pugicast.h"
#include "script.h"

#include <boost/algorithm/string.hpp>
#include <fmt/format.h>

extern Game g_game;
extern LuaEnvironment g_luaEnvironment;
extern Scripts* g_scripts;

uint32_t Npc::npcAutoID = 0x80000000;

// NpcType registry
std::map<std::string, NpcType> Npcs::npcTypes;

NpcType* Npcs::getNpcType(const std::string& name, bool create)
{
	const std::string lowerName = boost::algorithm::to_lower_copy(name);
	auto it = npcTypes.find(lowerName);
	if (it != npcTypes.end()) {
		return &it->second;
	}
	if (create) {
		NpcType& t = npcTypes[lowerName];
		t.name = name;
		t.nameDescription = name;
		return &t;
	}
	return nullptr;
}

bool NpcType::loadCallback(LuaScriptInterface* scriptInterface)
{
	const int32_t id = scriptInterface->getEvent();
	if (id == -1) {
		LOG_WARN("[Warning - NpcType::loadCallback] Event not found");
		return false;
	}
	info.scriptInterface = scriptInterface;
	switch (info.eventType) {
		case NPCS_EVENT_THINK:               info.thinkEvent = id; break;
		case NPCS_EVENT_APPEAR:              info.creatureAppearEvent = id; break;
		case NPCS_EVENT_DISAPPEAR:           info.creatureDisappearEvent = id; break;
		case NPCS_EVENT_MOVE:                info.creatureMoveEvent = id; break;
		case NPCS_EVENT_SAY:                 info.creatureSayEvent = id; break;
		case NPCS_EVENT_PLAYER_BUY:          info.playerBuyEvent = id; break;
		case NPCS_EVENT_PLAYER_SELL:         info.playerSellEvent = id; break;
		case NPCS_EVENT_PLAYER_CHECK_ITEM:   info.playerCheckEvent = id; break;
		case NPCS_EVENT_PLAYER_CLOSE_CHANNEL: info.playerCloseChannelEvent = id; break;
		default:
			LOG_WARN("[Warning - NpcType::loadCallback] Unknown event type");
			return false;
	}
	return true;
}

void Npcs::reload()
{
	const std::unordered_map<uint32_t, Npc*>& npcs = g_game.getNpcs();
	for (const auto& it : npcs) {
		it.second->closeAllShopWindows();
	}

	for (const auto& it : npcs) {
		it.second->reload();
	}
}

std::unique_ptr<Npc> Npc::createNpc(const std::string &name)
{
	std::unique_ptr<Npc> npc = std::make_unique<Npc>(name);
	if (!npc->load()) {
		return nullptr;
	}
	return npc;
}

Npc::Npc(const std::string& npcName) : Creature(), name(npcName), filename("data/npc/" + npcName + ".xml"), masterRadius(-1), loaded(false)
{
	reset();
}

Npc::~Npc()
{
	parameters.clear();
	reset(); 
}

void Npc::addList() { g_game.addNpc(this); }

void Npc::removeList() { g_game.removeNpc(this); }

bool Npc::load()
{
	if (loaded) {
		return true;
	}

	reset();

	// Check NpcType registry (RevScriptSys only — XML loading disabled)
	npcType = Npcs::getNpcType(name);
	if (npcType) {
		loaded = loadFromLuaType();
		return loaded;
	}

	LOG_WARN("[Warning - Npc::load] NPC '{}' not found in RevScriptSys registry. Add a script in data/npc_revscript/", name);
	return false;
}

bool Npc::loadFromLuaType()
{
	if (!npcType) {
		return false;
	}
	name = npcType->name;
	health = npcType->info.health;
	healthMax = npcType->info.healthMax;
	baseSpeed = npcType->info.baseSpeed;
	pushable = npcType->info.pushable;
	attackable = npcType->info.attackable;
	floorChange = npcType->info.floorChange;
	ignoreHeight = npcType->info.ignoreHeight;
	walkTicks = npcType->info.walkInterval;
	masterRadius = npcType->info.walkRadius;
	speechBubble = npcType->info.speechBubble;
	moneyType = npcType->info.moneyType;
	defaultOutfit = npcType->info.outfit;
	currentOutfit = defaultOutfit;
	return true;
}

void Npc::reset()
{
	loaded = false;
	isIdle = true;
	walkTicks = 1500;
	pushable = true;
	floorChange = false;
	attackable = false;
	ignoreHeight = false;
	focusCreature = 0;

	moneyType = 0;

	npcType = nullptr;
	playerInteractions.clear();

	parameters.clear();
	shopPlayerSet.clear();
	spectators.clear();
}

void Npc::reload()
{
	reset();
	load();

	SpectatorVec players;
	g_game.map.getSpectators(players, getPosition(), true, true);
	for (const auto& player : players) {
		assert(dynamic_cast<Player*>(player) != nullptr);
		spectators.insert(static_cast<Player*>(player));
	}

	const bool hasSpectators = !spectators.empty();
	setIdle(!hasSpectators);

	if (hasSpectators && walkTicks > 0) {
		addEventWalk();
	}

	// Simulate that the creature is placed on the map again.
	if (npcType && npcType->info.creatureAppearEvent != -1) {
		LuaScriptInterface* si = npcType->info.scriptInterface;
		if (si && si->reserveScriptEnv()) {
			ScriptEnvironment* env = si->getScriptEnv();
			env->setScriptId(npcType->info.creatureAppearEvent, si);
			env->setNpc(this);
			lua_State* L = si->getLuaState();
			si->pushFunction(npcType->info.creatureAppearEvent);
			Lua::pushUserdata<Npc>(L, this);
			Lua::setCreatureMetatable(L, -1, this);
			Lua::pushUserdata<Npc>(L, this);
			Lua::setCreatureMetatable(L, -1, this);
			si->callVoidFunction(2);
		}
	}
}

bool Npc::canSee(const Position& pos) const
{
	if (pos.z != getPosition().z) {
		return false;
	}
	return Creature::canSee(getPosition(), pos, 3, 3);
}

std::string Npc::getDescription(int32_t) const
{
	std::string descr;
	descr.reserve(name.length() + 1);
	descr.assign(name);
	descr.push_back('.');
	return descr;
}

void Npc::onCreatureAppear(Creature* creature, bool isLogin)
{
	Creature::onCreatureAppear(creature, isLogin);

	if (creature == this) {
		SpectatorVec players;
		g_game.map.getSpectators(players, getPosition(), true, true);
		for (const auto& player : players) {
			assert(dynamic_cast<Player*>(player) != nullptr);
			spectators.insert(static_cast<Player*>(player));
		}

		const bool hasSpectators = !spectators.empty();
		setIdle(!hasSpectators);

		if (hasSpectators && walkTicks > 0) {
			addEventWalk();
		}

		if (npcType && npcType->info.creatureAppearEvent != -1) {
			LuaScriptInterface* si = npcType->info.scriptInterface;
			if (si && si->reserveScriptEnv()) {
				ScriptEnvironment* env = si->getScriptEnv();
				env->setScriptId(npcType->info.creatureAppearEvent, si);
				env->setNpc(this);
				lua_State* L = si->getLuaState();
				si->pushFunction(npcType->info.creatureAppearEvent);
				Lua::pushUserdata<Npc>(L, this);
				Lua::setCreatureMetatable(L, -1, this);
				Lua::pushUserdata<Npc>(L, this);
				Lua::setCreatureMetatable(L, -1, this);
				si->callVoidFunction(2);
			}
		}
	} else if (Player* player = creature->getPlayer()) {
		if (npcType && npcType->info.creatureAppearEvent != -1) {
			LuaScriptInterface* si = npcType->info.scriptInterface;
			if (si && si->reserveScriptEnv()) {
				ScriptEnvironment* env = si->getScriptEnv();
				env->setScriptId(npcType->info.creatureAppearEvent, si);
				env->setNpc(this);
				lua_State* L = si->getLuaState();
				si->pushFunction(npcType->info.creatureAppearEvent);
				Lua::pushUserdata<Npc>(L, this);
				Lua::setCreatureMetatable(L, -1, this);
				Lua::pushUserdata<Creature>(L, creature);
				Lua::setCreatureMetatable(L, -1, creature);
				si->callVoidFunction(2);
			}
		}

		spectators.insert(player);
		setIdle(false);
	}
}

void Npc::onRemoveCreature(Creature* creature, bool isLogout)
{
	Creature::onRemoveCreature(creature, isLogout);

	if (creature == this) {
		closeAllShopWindows();
		if (npcType && npcType->info.creatureDisappearEvent != -1) {
			LuaScriptInterface* si = npcType->info.scriptInterface;
			if (si && si->reserveScriptEnv()) {
				ScriptEnvironment* env = si->getScriptEnv();
				env->setScriptId(npcType->info.creatureDisappearEvent, si);
				env->setNpc(this);
				lua_State* L = si->getLuaState();
				si->pushFunction(npcType->info.creatureDisappearEvent);
				Lua::pushUserdata<Npc>(L, this);
				Lua::setCreatureMetatable(L, -1, this);
				Lua::pushUserdata<Npc>(L, this);
				Lua::setCreatureMetatable(L, -1, this);
				si->callVoidFunction(2);
			}
		}
	} else if (Player* player = creature->getPlayer()) {
		if (npcType && npcType->info.creatureDisappearEvent != -1) {
			LuaScriptInterface* si = npcType->info.scriptInterface;
			if (si && si->reserveScriptEnv()) {
				ScriptEnvironment* env = si->getScriptEnv();
				env->setScriptId(npcType->info.creatureDisappearEvent, si);
				env->setNpc(this);
				lua_State* L = si->getLuaState();
				si->pushFunction(npcType->info.creatureDisappearEvent);
				Lua::pushUserdata<Npc>(L, this);
				Lua::setCreatureMetatable(L, -1, this);
				Lua::pushUserdata<Creature>(L, creature);
				Lua::setCreatureMetatable(L, -1, creature);
				si->callVoidFunction(2);
			}
		}

		spectators.erase(player);
		setIdle(spectators.empty());
	}
}

void Npc::onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile,
                         const Position& oldPos, bool teleport)
{
	Creature::onCreatureMove(creature, newTile, newPos, oldTile, oldPos, teleport);

	if (creature == this || creature->getPlayer()) {
		if (npcType && npcType->info.creatureMoveEvent != -1) {
			LuaScriptInterface* si = npcType->info.scriptInterface;
			if (si && si->reserveScriptEnv()) {
				ScriptEnvironment* env = si->getScriptEnv();
				env->setScriptId(npcType->info.creatureMoveEvent, si);
				env->setNpc(this);
				lua_State* L = si->getLuaState();
				si->pushFunction(npcType->info.creatureMoveEvent);
				Lua::pushUserdata<Npc>(L, this);
				Lua::setCreatureMetatable(L, -1, this);
				Lua::pushUserdata<Creature>(L, creature);
				Lua::setCreatureMetatable(L, -1, creature);
				Lua::pushPosition(L, oldPos);
				Lua::pushPosition(L, newPos);
				si->callVoidFunction(4);
			}
		}

		if (creature != this) {
			Player* player = creature->getPlayer();

			// if player is now in range, add to spectators list, otherwise erase
			if (player->canSee(position)) {
				spectators.insert(player);
			} else {
				spectators.erase(player);
			}

			setIdle(spectators.empty());
		}
	}
}

void Npc::onCreatureSay(Creature* creature, SpeakClasses type, std::string_view text)
{
	if (creature == this) {
		return;
	}

	// only players for script events
	Player* player = creature->getPlayer();
	if (player) {
		if (npcType && npcType->info.creatureSayEvent != -1) {
			LuaScriptInterface* si = npcType->info.scriptInterface;
			if (si && si->reserveScriptEnv()) {
				ScriptEnvironment* env = si->getScriptEnv();
				env->setScriptId(npcType->info.creatureSayEvent, si);
				env->setNpc(this);
				lua_State* L = si->getLuaState();
				si->pushFunction(npcType->info.creatureSayEvent);
				Lua::pushUserdata<Npc>(L, this);
				Lua::setCreatureMetatable(L, -1, this);
				Lua::pushUserdata<Creature>(L, creature);
				Lua::setCreatureMetatable(L, -1, creature);
				lua_pushinteger(L, type);
				Lua::pushString(L, text);
				si->callVoidFunction(4);
			}
		}
	}
}

void Npc::onPlayerCloseChannel(Player* player)
{
	if (npcType && npcType->info.playerCloseChannelEvent != -1) {
		LuaScriptInterface* si = npcType->info.scriptInterface;
		if (si && si->reserveScriptEnv()) {
			ScriptEnvironment* env = si->getScriptEnv();
			env->setScriptId(npcType->info.playerCloseChannelEvent, si);
			env->setNpc(this);
			lua_State* L = si->getLuaState();
			si->pushFunction(npcType->info.playerCloseChannelEvent);
			Lua::pushUserdata<Npc>(L, this);
			Lua::setCreatureMetatable(L, -1, this);
			Lua::pushUserdata<Player>(L, player);
			Lua::setMetatable(L, -1, "Player");
			si->callVoidFunction(2);
		}
	}
}

void Npc::onThink(uint32_t interval)
{
	Creature::onThink(interval);

	if (npcType && npcType->info.thinkEvent != -1) {
		LuaScriptInterface* si = npcType->info.scriptInterface;
		if (si && si->reserveScriptEnv()) {
			ScriptEnvironment* env = si->getScriptEnv();
			env->setScriptId(npcType->info.thinkEvent, si);
			env->setNpc(this);
			lua_State* L = si->getLuaState();
			si->pushFunction(npcType->info.thinkEvent);
			Lua::pushUserdata<Npc>(L, this);
			Lua::setCreatureMetatable(L, -1, this);
			lua_pushinteger(L, interval);
			si->callVoidFunction(2);
		}
	}

	if (!isIdle && getTimeSinceLastMove() >= walkTicks) {
		addEventWalk();
	}
}

void Npc::doSay(std::string_view text) { g_game.internalCreatureSay(this, TALKTYPE_SAY, text, false); }

void Npc::doSayToPlayer(Player* player, std::string_view text)
{
	if (player) {
		player->sendCreatureSay(this, TALKTYPE_PRIVATE_NP, text);
		player->onCreatureSay(this, TALKTYPE_PRIVATE_NP, text);
	}
}

void Npc::onPlayerTrade(Player* player, int32_t callback, uint16_t itemId, uint8_t count, uint8_t amount,
                        bool ignore /* = false*/, bool inBackpacks /* = false*/)
{
	// Sentinel values -2=buy and -3=sell indicate NpcType-based shop callbacks
	if (npcType) {
		if (callback == -2 && npcType->info.playerBuyEvent != -1) {
			LuaScriptInterface* si = npcType->info.scriptInterface;
			if (si && si->reserveScriptEnv()) {
				ScriptEnvironment* env = si->getScriptEnv();
				env->setScriptId(npcType->info.playerBuyEvent, si);
				env->setNpc(this);
				lua_State* L = si->getLuaState();
				si->pushFunction(npcType->info.playerBuyEvent);
				Lua::pushUserdata<Npc>(L, this);
				Lua::setCreatureMetatable(L, -1, this);
				Lua::pushUserdata<Player>(L, player);
				Lua::setMetatable(L, -1, "Player");
				lua_pushinteger(L, itemId);
				lua_pushinteger(L, count);
				lua_pushinteger(L, amount);
				Lua::pushBoolean(L, ignore);
				Lua::pushBoolean(L, inBackpacks);
				lua_pushinteger(L, 0); // totalCost placeholder
				si->callVoidFunction(8);
			}
			player->sendSaleItemList();
			return;
		} else if (callback == -3 && npcType->info.playerSellEvent != -1) {
			LuaScriptInterface* si = npcType->info.scriptInterface;
			if (si && si->reserveScriptEnv()) {
				ScriptEnvironment* env = si->getScriptEnv();
				env->setScriptId(npcType->info.playerSellEvent, si);
				env->setNpc(this);
				lua_State* L = si->getLuaState();
				si->pushFunction(npcType->info.playerSellEvent);
				Lua::pushUserdata<Npc>(L, this);
				Lua::setCreatureMetatable(L, -1, this);
				Lua::pushUserdata<Player>(L, player);
				Lua::setMetatable(L, -1, "Player");
				lua_pushinteger(L, itemId);
				lua_pushinteger(L, count);
				lua_pushinteger(L, amount);
				Lua::pushBoolean(L, ignore);
				Lua::pushBoolean(L, inBackpacks);
				lua_pushinteger(L, 0); // totalCost placeholder
				si->callVoidFunction(8);
			}
			player->sendSaleItemList();
			return;
		}
	}
	player->sendSaleItemList();
}

void Npc::onPlayerEndTrade(Player* player, int32_t buyCallback, int32_t sellCallback)
{
	// Only unref actual Lua registry refs (positive values)
	LuaScriptInterface* si = npcType ? npcType->info.scriptInterface : nullptr;
	if (si) {
		if (buyCallback > 0) {
			luaL_unref(si->getLuaState(), LUA_REGISTRYINDEX, buyCallback);
		}
		if (sellCallback > 0) {
			luaL_unref(si->getLuaState(), LUA_REGISTRYINDEX, sellCallback);
		}
	}

	removeShopPlayer(player);
}

bool Npc::getNextStep(Direction& dir, uint32_t& flags)
{
	if (Creature::getNextStep(dir, flags)) {
		return true;
	}

	if (walkTicks == 0) {
		return false;
	}

	if (focusCreature != 0) {
		return false;
	}

	if (getTimeSinceLastMove() < walkTicks) {
		return false;
	}

	return getRandomStep(dir);
}

void Npc::setIdle(const bool idle)
{
	if (idle == isIdle) {
		return;
	}

	if (isRemoved() || isDead()) {
		return;
	}

	isIdle = idle;

	if (isIdle) {
		onIdleStatus();
	}
}

bool Npc::canWalkTo(const Position& fromPos, Direction dir) const
{
	if (masterRadius == 0) {
		return false;
	}

	Position toPos = getNextPosition(dir, fromPos);
	if (!Spawns::isInZone(masterPos, masterRadius, toPos)) {
		return false;
	}

	Tile* tile = g_game.map.getTile(toPos);
	if (!tile || tile->queryAdd(0, *this, 1, 0) != RETURNVALUE_NOERROR) {
		return false;
	}

	if (!floorChange && (tile->hasFlag(TILESTATE_FLOORCHANGE) || tile->getTeleportItem())) {
		return false;
	}

	if (!ignoreHeight && tile->hasHeight(1)) {
		return false;
	}

	return true;
}

bool Npc::getRandomStep(Direction& direction) const
{
	const Position& creaturePos = getPosition();
	for (Direction dir : getShuffleDirections()) {
		if (canWalkTo(creaturePos, dir)) {
			direction = dir;
			return true;
		}
	}
	return false;
}

bool Npc::doMoveTo(const Position& pos, int32_t minTargetDist /* = 1*/, int32_t maxTargetDist /* = 1*/,
                   bool fullPathSearch /* = true*/, bool clearSight /* = true*/, int32_t maxSearchDist /* = 0*/)
{
	listWalkDir.clear();
	if (getPathTo(pos, listWalkDir, minTargetDist, maxTargetDist, fullPathSearch, clearSight, maxSearchDist)) {
		startAutoWalk();
		return true;
	}
	return false;
}

void Npc::turnToCreature(Creature* creature)
{
	const Position& creaturePos = creature->getPosition();
	const Position& myPos = getPosition();
	const auto dx = myPos.getOffsetX(creaturePos);
	const auto dy = myPos.getOffsetY(creaturePos);

	float tan;
	if (dx != 0) {
		tan = static_cast<float>(dy) / dx;
	} else {
		tan = 10;
	}

	Direction dir;
	if (std::abs(tan) < 1) {
		if (dx > 0) {
			dir = DIRECTION_WEST;
		} else {
			dir = DIRECTION_EAST;
		}
	} else {
		if (dy > 0) {
			dir = DIRECTION_NORTH;
		} else {
			dir = DIRECTION_SOUTH;
		}
	}
	g_game.internalCreatureTurn(this, dir);
}

void Npc::setCreatureFocus(Creature* creature)
{
	if (creature) {
		focusCreature = creature->getID();
		turnToCreature(creature);
	} else {
		focusCreature = 0;
	}
}

void Npc::addShopPlayer(Player* player) { shopPlayerSet.insert(player); }

void Npc::removeShopPlayer(Player* player) { shopPlayerSet.erase(player); }

void Npc::closeAllShopWindows()
{
	while (!shopPlayerSet.empty()) {
		Player* player = *shopPlayerSet.begin();
		if (!player->closeShopWindow()) {
			removeShopPlayer(player);
		}
	}
}

void Npc::setPlayerInteraction(uint32_t playerId, int32_t topic)
{
	playerInteractions[playerId] = topic;
}

bool Npc::isInteractingWithPlayer(uint32_t playerId) const
{
	return playerInteractions.count(playerId) != 0;
}

void Npc::removePlayerInteraction(uint32_t playerId)
{
	playerInteractions.erase(playerId);
}


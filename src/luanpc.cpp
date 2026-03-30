// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "game.h"
#include "items.h"
#include "luascript.h"
#include "npc.h"
#include "script.h"

extern Game g_game;
extern Scripts* g_scripts;

namespace {
using namespace Lua;

// Npc
int luaNpcCreate(lua_State* L)
{
	// Npc([id or name or userdata])
	Npc* npc;
	if (lua_gettop(L) >= 2) {
		if (isInteger(L, 2)) {
			npc = g_game.getNpcByID(getInteger<uint32_t>(L, 2));
		} else if (isString(L, 2)) {
			npc = g_game.getNpcByName(getString(L, 2));
		} else if (isUserdata(L, 2)) {
			npc = getUserdata<Npc>(L, 2);
		} else {
			npc = nullptr;
		}
	} else {
		npc = LuaScriptInterface::getScriptEnv()->getNpc();
	}

	if (npc) {
		pushUserdata<Npc>(L, npc);
		setCreatureMetatable(L, -1, npc);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int luaNpcIsNpc(lua_State* L)
{
	// npc:isNpc()
	pushBoolean(L, getUserdata<const Npc>(L, 1) != nullptr);
	return 1;
}

int luaNpcSetMasterPos(lua_State* L)
{
	// npc:setMasterPos(pos[, radius])
	Npc* npc = getUserdata<Npc>(L, 1);
	if (!npc) {
		lua_pushnil(L);
		return 1;
	}

	const Position& pos = getPosition(L, 2);
	int32_t radius = getInteger<int32_t>(L, 3, 1);
	npc->setMasterPos(pos, radius);
	pushBoolean(L, true);
	return 1;
}

int luaNpcGetSpectators(lua_State* L)
{
	// npc:getSpectators()
	Npc* npc = getUserdata<Npc>(L, 1);
	if (!npc) {
		lua_pushnil(L);
		return 1;
	}

	const auto& spectators = npc->getSpectators();
	lua_createtable(L, spectators.size(), 0);

	int index = 0;
	for (const auto& spectatorPlayer : npc->getSpectators()) {
		pushUserdata<const Player>(L, spectatorPlayer);
		setCreatureMetatable(L, -1, spectatorPlayer);
		lua_rawseti(L, -2, ++index);
	}
	return 1;
}

// npc:say(text[, type[, ghost[, target]]])
int luaNpcSay(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	if (!npc) {
		lua_pushnil(L);
		return 1;
	}

	const std::string& text = getString(L, 2);
	SpeakClasses type = getInteger<SpeakClasses>(L, 3, TALKTYPE_PRIVATE_NP);
	bool ghost = getBoolean(L, 4, false);

	// Arg 5 can be Player userdata OR integer (player UID from sayWithDelay)
	Creature* target = nullptr;
	if (isUserdata(L, 5)) {
		target = getCreature(L, 5);
	} else if (isInteger(L, 5)) {
		target = g_game.getCreatureByID(getInteger<uint32_t>(L, 5));
	}

	if (target) {
		Player* player = target->getPlayer();
		if (player) {
			player->sendCreatureSay(npc, type, text);
			player->onCreatureSay(npc, type, text);
		}
	} else {
		g_game.internalCreatureSay(npc, type, text, ghost);
	}
	pushBoolean(L, true);
	return 1;
}

// npc:openShopWindow(player[, items[, buyCallback[, sellCallback]]])
int luaNpcOpenShopWindow(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	Player* player = getPlayer(L, 2);
	if (!npc || !player) {
		pushBoolean(L, false);
		return 1;
	}

	player->closeShopWindow(false);

	int top = lua_gettop(L);
	std::list<ShopInfo> items;
	int32_t buyCallback = -1;
	int32_t sellCallback = -1;

	if (top >= 3 && isTable(L, 3)) {
		// items list provided
		lua_pushnil(L);
		while (lua_next(L, 3) != 0) {
			const auto tableIndex = lua_gettop(L);
			ShopInfo item;
			item.itemId = getField<uint16_t>(L, tableIndex, "id");
			item.subType = getField<int32_t>(L, tableIndex, "subType");
			if (item.subType == 0) {
				item.subType = getField<int32_t>(L, tableIndex, "subtype");
				lua_pop(L, 1);
			}
			item.buyPrice = getField<int64_t>(L, tableIndex, "buy");
			item.sellPrice = getField<int64_t>(L, tableIndex, "sell");
			item.realName = getFieldString(L, tableIndex, "name");
			items.push_back(item);
			lua_pop(L, 6);
		}

		if (top >= 5 && isFunction(L, 5)) {
			sellCallback = luaL_ref(L, LUA_REGISTRYINDEX);
		} else if (top >= 5) {
			lua_pop(L, 1);
		}
		if (top >= 4 && isFunction(L, 4)) {
			buyCallback = luaL_ref(L, LUA_REGISTRYINDEX);
		} else if (top >= 4) {
			lua_pop(L, 1);
		}
	} else if (npc->getNpcType()) {
		// use NpcType shop items with sentinel callbacks
		for (const auto& si : npc->getNpcType()->info.shopItems) {
			items.push_back(si);
		}
		buyCallback = -2;  // sentinel: dispatch NPCS_EVENT_PLAYER_BUY
		sellCallback = -3; // sentinel: dispatch NPCS_EVENT_PLAYER_SELL
	}

	npc->openShopForPlayer(player);
	player->setShopOwner(npc, buyCallback, sellCallback);
	player->openShopWindow(items);
	pushBoolean(L, true);
	return 1;
}

// npc:closeShopWindow(player)
int luaNpcCloseShopWindow(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	Player* player = getPlayer(L, 2);
	if (!npc || !player) {
		pushBoolean(L, false);
		return 1;
	}

	int32_t buyCallback, sellCallback;
	Npc* merchant = player->getShopOwner(buyCallback, sellCallback);
	if (merchant == npc) {
		player->sendCloseShop();
		// Only unref actual Lua refs (positive values, not sentinels -2/-3)
		LuaScriptInterface* si = npc->getShopScriptInterface();
		if (si) {
			if (buyCallback > 0) {
				luaL_unref(si->getLuaState(), LUA_REGISTRYINDEX, buyCallback);
			}
			if (sellCallback > 0) {
				luaL_unref(si->getLuaState(), LUA_REGISTRYINDEX, sellCallback);
			}
		}
		player->setShopOwner(nullptr, -1, -1);
		npc->closeShopForPlayer(player);
	}
	pushBoolean(L, true);
	return 1;
}

// npc:sellItem(player, itemId, amount[, subType[, actionId[, canDropOnMap[, inBackpacks]]]])
int luaNpcSellItem(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	Player* player = getPlayer(L, 2);
	if (!npc || !player) {
		lua_pushinteger(L, 0);
		return 1;
	}

	uint32_t itemId = getInteger<uint32_t>(L, 3);
	uint32_t amount = getInteger<uint32_t>(L, 4, 1);
	uint32_t subType = getInteger<uint32_t>(L, 5, 1);
	uint32_t actionId = getInteger<uint32_t>(L, 6, 0);
	bool canDropOnMap = getBoolean(L, 7, true);
	(void)getBoolean(L, 8, false); // inBackpacks — accepted but not used (items go directly to inventory)

	uint32_t sellCount = 0;
	const ItemType& it = Item::items[itemId];
	if (it.stackable) {
		while (amount > 0) {
			int32_t stackCount = std::min<int32_t>(amount, it.stackSize);
			Item* item = Item::CreateItem(it.id, static_cast<uint16_t>(stackCount));
			if (item && actionId != 0) {
				item->setActionId(static_cast<uint16_t>(actionId));
			}
			if (g_game.internalPlayerAddItem(player, item, canDropOnMap) != RETURNVALUE_NOERROR) {
				delete item;
				lua_pushinteger(L, sellCount);
				return 1;
			}
			amount -= stackCount;
			sellCount += stackCount;
		}
	} else {
		for (uint32_t i = 0; i < amount; ++i) {
			Item* item = Item::CreateItem(it.id, static_cast<uint16_t>(subType));
			if (item && actionId != 0) {
				item->setActionId(static_cast<uint16_t>(actionId));
			}
			if (g_game.internalPlayerAddItem(player, item, canDropOnMap) != RETURNVALUE_NOERROR) {
				delete item;
				lua_pushinteger(L, sellCount);
				return 1;
			}
			++sellCount;
		}
	}
	lua_pushinteger(L, sellCount);
	return 1;
}

// npc:setPlayerInteraction(player, topicId)
int luaNpcSetPlayerInteraction(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	Player* player = getPlayer(L, 2);
	if (!npc || !player) {
		pushBoolean(L, false);
		return 1;
	}
	int32_t topicId = getInteger<int32_t>(L, 3, 0);
	npc->setPlayerInteraction(player->getID(), topicId);
	// Also set creature focus so npc turns to face player
	npc->setCreatureFocus(player);
	pushBoolean(L, true);
	return 1;
}

// npc:isInteractingWithPlayer(player)
int luaNpcIsInteractingWithPlayer(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	Player* player = getPlayer(L, 2);
	if (!npc || !player) {
		pushBoolean(L, false);
		return 1;
	}
	pushBoolean(L, npc->isInteractingWithPlayer(player->getID()));
	return 1;
}

// npc:removePlayerInteraction(player)
int luaNpcRemovePlayerInteraction(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	Player* player = getPlayer(L, 2);
	if (!npc || !player) {
		pushBoolean(L, false);
		return 1;
	}
	npc->removePlayerInteraction(player->getID());
	pushBoolean(L, true);
	return 1;
}

// npc:turnToCreature(creature)
int luaNpcTurnToCreature(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	Creature* creature = getCreature(L, 2);
	if (!npc) {
		pushBoolean(L, false);
		return 1;
	}
	npc->setCreatureFocus(creature);
	pushBoolean(L, true);
	return 1;
}

// npc:isMerchant()
int luaNpcIsMerchant(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	pushBoolean(L, npc ? npc->isMerchant() : false);
	return 1;
}

// npc:isInTalkRange(position[, range])
int luaNpcIsInTalkRange(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	if (!npc) {
		pushBoolean(L, false);
		return 1;
	}
	const Position& pos = getPosition(L, 2);
	int32_t range = getInteger<int32_t>(L, 3, 4);
	const Position& npcPos = npc->getPosition();
	bool inRange = (npcPos.z == pos.z)
	    && (std::abs(npcPos.getDistanceX(pos)) <= range)
	    && (std::abs(npcPos.getDistanceY(pos)) <= range);
	pushBoolean(L, inRange);
	return 1;
}

// npc:getDistanceTo(creature)
int luaNpcGetDistanceTo(lua_State* L)
{
	Npc* npc = getUserdata<Npc>(L, 1);
	Creature* creature = getCreature(L, 2);
	if (!npc || !creature) {
		lua_pushinteger(L, -1);
		return 1;
	}
	const Position& npcPos = npc->getPosition();
	const Position& crePos = creature->getPosition();
	if (npcPos.z != crePos.z) {
		lua_pushinteger(L, -1);
	} else {
		lua_pushinteger(L, std::max(npcPos.getDistanceX(crePos), npcPos.getDistanceY(crePos)));
	}
	return 1;
}

// ── NpcType ──────────────────────────────────────────────────────────────────

int luaNpcTypeCreate(lua_State* L)
{
	// NpcType(name) — only callable from Scripts interface
	if (LuaScriptInterface::getScriptEnv()->getScriptInterface() != &g_scripts->getScriptInterface()) {
		LuaScriptInterface::reportErrorFunc(L, "NpcTypes can only be registered in the Scripts interface.");
		lua_pushnil(L);
		return 1;
	}

	const std::string& name = getString(L, 1);
	if (name.empty()) {
		lua_pushnil(L);
		return 1;
	}

	NpcType* npcType = Npcs::getNpcType(name, true);
	// Reset events on re-registration
	npcType->info.thinkEvent = -1;
	npcType->info.creatureAppearEvent = -1;
	npcType->info.creatureDisappearEvent = -1;
	npcType->info.creatureMoveEvent = -1;
	npcType->info.creatureSayEvent = -1;
	npcType->info.playerBuyEvent = -1;
	npcType->info.playerSellEvent = -1;
	npcType->info.playerCheckEvent = -1;
	npcType->info.playerCloseChannelEvent = -1;

	pushUserdata<NpcType>(L, npcType);
	setMetatable(L, -1, "NpcType");
	return 1;
}

int luaNpcTypeEventType(lua_State* L)
{
	// npcType:eventType(type)
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		pushBoolean(L, false);
		return 1;
	}
	npcType->info.eventType = getInteger<NpcsEvent_t>(L, 2);
	pushBoolean(L, true);
	return 1;
}

int luaNpcTypeEventOnCallback(lua_State* L)
{
	// npcType:onThink/onAppear/…(function)
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) {
		pushBoolean(L, false);
		return 1;
	}
	if (LuaScriptInterface::getScriptEnv()->getScriptInterface() != &g_scripts->getScriptInterface()) {
		LuaScriptInterface::reportErrorFunc(L, "NpcType callbacks can only be registered in the Scripts interface.");
		pushBoolean(L, false);
		return 1;
	}
	if (lua_isfunction(L, 2)) {
		npcType->loadCallback(&g_scripts->getScriptInterface());
		pushBoolean(L, true);
	} else {
		pushBoolean(L, false);
	}
	return 1;
}

int luaNpcTypeName(lua_State* L)
{
	// get: npcType:name()  set: npcType:name(name)
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		pushString(L, npcType->name);
	} else {
		npcType->name = getString(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeNameDescription(lua_State* L)
{
	// get: npcType:nameDescription()  set: npcType:nameDescription(desc)
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		pushString(L, npcType->nameDescription);
	} else {
		npcType->nameDescription = getString(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeHealth(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->info.health);
	} else {
		npcType->info.health = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeMaxHealth(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->info.healthMax);
	} else {
		npcType->info.healthMax = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeBaseSpeed(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->info.baseSpeed);
	} else {
		npcType->info.baseSpeed = getInteger<uint32_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeWalkInterval(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->info.walkInterval);
	} else {
		npcType->info.walkInterval = getInteger<uint32_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeWalkRadius(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->info.walkRadius);
	} else {
		npcType->info.walkRadius = getInteger<int32_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeSpeechBubble(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->info.speechBubble);
	} else {
		npcType->info.speechBubble = getInteger<uint8_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeCurrency(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		lua_pushinteger(L, npcType->info.moneyType);
	} else {
		npcType->info.moneyType = getInteger<uint16_t>(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeOutfit(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		pushOutfit(L, npcType->info.outfit);
	} else {
		npcType->info.outfit = getOutfit(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeIsPushable(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		pushBoolean(L, npcType->info.pushable);
	} else {
		npcType->info.pushable = getBoolean(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeFloorChange(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		pushBoolean(L, npcType->info.floorChange);
	} else {
		npcType->info.floorChange = getBoolean(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeAttackable(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		pushBoolean(L, npcType->info.attackable);
	} else {
		npcType->info.attackable = getBoolean(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

int luaNpcTypeIgnoreHeight(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType) { lua_pushnil(L); return 1; }
	if (lua_gettop(L) == 1) {
		pushBoolean(L, npcType->info.ignoreHeight);
	} else {
		npcType->info.ignoreHeight = getBoolean(L, 2);
		pushBoolean(L, true);
	}
	return 1;
}

// npcType:addShopItem({id, clientId, subType, buy, sell, name})
// Accepts either 'id' (server ID) or 'clientId' (sprite ID).
int luaNpcTypeAddShopItem(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (!npcType || !isTable(L, 2)) {
		pushBoolean(L, false);
		return 1;
	}

	ShopInfo item;
	const auto tableIndex = 2;

	// Try server ID first, fall back to clientId lookup
	uint16_t serverId = getField<uint16_t>(L, tableIndex, "id");
	lua_pop(L, 1);
	if (serverId == 0) {
		uint16_t clientId = getField<uint16_t>(L, tableIndex, "clientId");
		lua_pop(L, 1);
		if (clientId != 0) {
			const ItemType& it = Item::items.getItemIdByClientId(clientId);
			serverId = it.id;
		}
	}

	item.itemId = serverId;
	item.subType = getField<int32_t>(L, tableIndex, "subType");
	if (item.subType == 0) {
		item.subType = getField<int32_t>(L, tableIndex, "subtype");
		lua_pop(L, 1);
	} else {
		lua_pop(L, 1);
	}
	item.buyPrice = getField<int64_t>(L, tableIndex, "buy");
	item.sellPrice = getField<int64_t>(L, tableIndex, "sell");
	item.realName = getFieldString(L, tableIndex, "name");
	lua_pop(L, 3);

	npcType->info.shopItems.push_back(item);
	pushBoolean(L, true);
	return 1;
}

int luaNpcTypeGetName(lua_State* L)
{
	NpcType* npcType = getUserdata<NpcType>(L, 1);
	if (npcType) {
		pushString(L, npcType->name);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

} // namespace

void LuaScriptInterface::registerNpc()
{
	// Npc
	registerClass("Npc", "Creature", luaNpcCreate);
	registerMetaMethod("Npc", "__eq", LuaScriptInterface::luaUserdataCompare);
	registerMetaMethod("Npc", "__gc", LuaScriptInterface::luaCreatureGC);

	registerMethod("Npc", "isNpc", luaNpcIsNpc);

	registerMethod("Npc", "setMasterPos", luaNpcSetMasterPos);

	registerMethod("Npc", "getSpectators", luaNpcGetSpectators);

	// RevScriptSys Npc methods
	registerMethod("Npc", "say", luaNpcSay);
	registerMethod("Npc", "openShopWindow", luaNpcOpenShopWindow);
	registerMethod("Npc", "closeShopWindow", luaNpcCloseShopWindow);
	registerMethod("Npc", "sellItem", luaNpcSellItem);
	registerMethod("Npc", "setPlayerInteraction", luaNpcSetPlayerInteraction);
	registerMethod("Npc", "isInteractingWithPlayer", luaNpcIsInteractingWithPlayer);
	registerMethod("Npc", "removePlayerInteraction", luaNpcRemovePlayerInteraction);
	registerMethod("Npc", "turnToCreature", luaNpcTurnToCreature);
	registerMethod("Npc", "isMerchant", luaNpcIsMerchant);
	registerMethod("Npc", "isInTalkRange", luaNpcIsInTalkRange);
	registerMethod("Npc", "getDistanceTo", luaNpcGetDistanceTo);

	// NpcType
	registerClass("NpcType", "", luaNpcTypeCreate);
	registerMetaMethod("NpcType", "__eq", LuaScriptInterface::luaUserdataCompare);

	registerMethod("NpcType", "eventType", luaNpcTypeEventType);
	registerMethod("NpcType", "onThink", luaNpcTypeEventOnCallback);
	registerMethod("NpcType", "onAppear", luaNpcTypeEventOnCallback);
	registerMethod("NpcType", "onDisappear", luaNpcTypeEventOnCallback);
	registerMethod("NpcType", "onMove", luaNpcTypeEventOnCallback);
	registerMethod("NpcType", "onSay", luaNpcTypeEventOnCallback);
	registerMethod("NpcType", "onCloseChannel", luaNpcTypeEventOnCallback);
	registerMethod("NpcType", "onBuyItem", luaNpcTypeEventOnCallback);
	registerMethod("NpcType", "onSellItem", luaNpcTypeEventOnCallback);
	registerMethod("NpcType", "onCheckItem", luaNpcTypeEventOnCallback);

	registerMethod("NpcType", "name", luaNpcTypeName);
	registerMethod("NpcType", "nameDescription", luaNpcTypeNameDescription);
	registerMethod("NpcType", "getName", luaNpcTypeGetName);
	registerMethod("NpcType", "health", luaNpcTypeHealth);
	registerMethod("NpcType", "maxHealth", luaNpcTypeMaxHealth);
	registerMethod("NpcType", "baseSpeed", luaNpcTypeBaseSpeed);
	registerMethod("NpcType", "walkInterval", luaNpcTypeWalkInterval);
	registerMethod("NpcType", "walkRadius", luaNpcTypeWalkRadius);
	registerMethod("NpcType", "speechBubble", luaNpcTypeSpeechBubble);
	registerMethod("NpcType", "currency", luaNpcTypeCurrency);
	registerMethod("NpcType", "outfit", luaNpcTypeOutfit);
	registerMethod("NpcType", "isPushable", luaNpcTypeIsPushable);
	registerMethod("NpcType", "floorChange", luaNpcTypeFloorChange);
	registerMethod("NpcType", "attackable", luaNpcTypeAttackable);
	registerMethod("NpcType", "ignoreHeight", luaNpcTypeIgnoreHeight);
	registerMethod("NpcType", "addShopItem", luaNpcTypeAddShopItem);
}

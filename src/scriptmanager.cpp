// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "scriptmanager.h"

#include "actions.h"
#include "chat.h"
#include "events.h"
#include "globalevent.h"
#include "movement.h"
#include "script.h"
#include "spells.h"
#include "talkaction.h"
#include "weapons.h"
#include "logger.h"

ScriptingManager& ScriptingManager::getInstance()
{
	static ScriptingManager instance;
	return instance;
}

Actions* g_actions = nullptr;
CreatureEvents* g_creatureEvents = nullptr;
Chat* g_chat = nullptr;
Events* g_events = nullptr;
GlobalEvents* g_globalEvents = nullptr;
Spells* g_spells = nullptr;
TalkActions* g_talkActions = nullptr;
MoveEvents* g_moveEvents = nullptr;
Weapons* g_weapons = nullptr;
Scripts* g_scripts = nullptr;

extern LuaEnvironment g_luaEnvironment;

ScriptingManager::~ScriptingManager()
{
	// Nullify globals before unique_ptrs destroy the objects
	g_events = nullptr;
	g_weapons = nullptr;
	g_spells = nullptr;
	g_actions = nullptr;
	g_talkActions = nullptr;
	g_moveEvents = nullptr;
	g_chat = nullptr;
	g_creatureEvents = nullptr;
	g_globalEvents = nullptr;
	g_scripts = nullptr;
	// unique_ptr members auto-destroy in reverse declaration order
}

bool ScriptingManager::loadPreItems()
{
	// Ensure g_luaEnvironment is properly initialized
	if (!g_luaEnvironment.getLuaState()) {
		if (!g_luaEnvironment.initState()) {
			LOG_ERROR("> ERROR: Failed to initialize Lua environment!");
			return false;
		}
	}

	if (!weapons_) {
		weapons_ = std::make_unique<Weapons>();
		g_weapons = weapons_.get();
	}
	if (!moveEvents_) {
		moveEvents_ = std::make_unique<MoveEvents>();
		g_moveEvents = moveEvents_.get();
	}

	return true;
}

bool ScriptingManager::loadScriptSystems()
{
	// Ensure g_luaEnvironment is properly initialized
	if (!g_luaEnvironment.getLuaState()) {
		if (!g_luaEnvironment.initState()) {
			LOG_ERROR("> ERROR: Failed to initialize Lua environment!");
			return false;
		}
	}

	if (g_luaEnvironment.loadFile("data/global.lua") == -1) {
		LOG_WARN("[Warning - ScriptingManager::loadScriptSystems] Can not load " "data/global.lua");
	}

	scripts_ = std::make_unique<Scripts>();
	g_scripts = scripts_.get();
	LOG_INFO(">> Loading lua libs");
	if (!g_scripts->loadScripts("scripts/lib", true, false)) {
		LOG_ERROR("> ERROR: Unable to load lua libs!");
		return false;
	}

	chat_ = std::make_unique<Chat>();
	g_chat = chat_.get();

	if (!g_scripts->loadScripts("items", false, false)) {
		LOG_ERROR("> ERROR: Unable to load items (LUA)!");
		return false;
	}

	if (!weapons_) {
		weapons_ = std::make_unique<Weapons>();
		g_weapons = weapons_.get();
	}
	g_weapons->loadDefaults();
	spells_ = std::make_unique<Spells>();
	g_spells = spells_.get();
	actions_ = std::make_unique<Actions>();
	g_actions = actions_.get();
	talkActions_ = std::make_unique<TalkActions>();
	g_talkActions = talkActions_.get();
	if (!moveEvents_) {
		moveEvents_ = std::make_unique<MoveEvents>();
		g_moveEvents = moveEvents_.get();
	}
	creatureEvents_ = std::make_unique<CreatureEvents>();
	g_creatureEvents = creatureEvents_.get();
	globalEvents_ = std::make_unique<GlobalEvents>();
	g_globalEvents = globalEvents_.get();

	events_ = std::make_unique<Events>();
	g_events = events_.get();
	if (!g_events->load()) {
		LOG_ERROR("> ERROR: Unable to load events!");
		return false;
	}

	// NPC RevScriptSys: load npclib and npc scripts
	if (g_luaEnvironment.loadFile("data/npclib/load.lua") == -1) {
		LOG_WARN("[Warning - ScriptingManager::loadScriptSystems] Cannot load data/npclib/load.lua");
	}
	g_scripts->loadScripts("npc_revscript", false, false);

	return true;
}

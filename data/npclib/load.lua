-- NPC RevScriptSys npclib loader
-- Compatibility shim: Crystal Server uses logger.X(), TFS 1.8 uses logInfo/logWarning/logError globals
if not logger then
	logger = {
		info = function(fmt, ...) logInfo(tostring(fmt)) end,
		warn = function(fmt, ...) logWarning(tostring(fmt)) end,
		error = function(fmt, ...) logError(tostring(fmt)) end,
	}
end

-- Compatibility shims for Crystal Server specific globals
if not Blessings then
	Blessings = {
		getBlessingCost = function(level, isMember, isUseSpecialBless) return 0 end,
		getPvpBlessingCost = function(level, isMember) return 0 end,
	}
end

if not IsTravelFree then
	IsTravelFree = function() return false end
end

-- Stub for Storage table used by custom_modules.lua
-- (Crystal Server specific; avoids nil index error on load)
if not Storage then
	Storage = setmetatable({}, {
		__index = function(t, k)
			local sub = setmetatable({}, getmetatable(t))
			rawset(t, k, sub)
			return sub
		end,
	})
end

dofile("data/npclib/npc.lua")
dofile("data/npclib/npc_system/npc_handler.lua")
dofile("data/npclib/npc_system/keyword_handler.lua")
dofile("data/npclib/npc_system/modules.lua")
dofile("data/npclib/npc_system/custom_modules.lua")
dofile("data/npclib/npc_system/bank_system.lua")

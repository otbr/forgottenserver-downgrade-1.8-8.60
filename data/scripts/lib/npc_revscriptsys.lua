-- NpcType __newindex for RevScriptSys event assignment
-- Enables: npcType.onSay = function(npc, creature, type, message) ... end
-- BUG FIX: Crystal Server revscriptsys.lua called self:onBuyItem(value) for onCloseChannel.
--          Fixed to call self:onCloseChannel(value) correctly.
do
	local function NpcTypeNewIndex(self, key, value)
		if key == "onThink" then
			self:eventType(NPCS_EVENT_THINK)
			self:onThink(value)
			return
		elseif key == "onAppear" then
			self:eventType(NPCS_EVENT_APPEAR)
			self:onAppear(value)
			return
		elseif key == "onDisappear" then
			self:eventType(NPCS_EVENT_DISAPPEAR)
			self:onDisappear(value)
			return
		elseif key == "onMove" then
			self:eventType(NPCS_EVENT_MOVE)
			self:onMove(value)
			return
		elseif key == "onSay" then
			self:eventType(NPCS_EVENT_SAY)
			self:onSay(value)
			return
		elseif key == "onBuyItem" then
			self:eventType(NPCS_EVENT_PLAYER_BUY)
			self:onBuyItem(value)
			return
		elseif key == "onSellItem" then
			self:eventType(NPCS_EVENT_PLAYER_SELL)
			self:onSellItem(value)
			return
		elseif key == "onCheckItem" then
			self:eventType(NPCS_EVENT_PLAYER_CHECK_ITEM)
			self:onCheckItem(value)
			return
		elseif key == "onCloseChannel" then
			self:eventType(NPCS_EVENT_PLAYER_CLOSE_CHANNEL)
			self:onCloseChannel(value)
			return
		end
		rawset(self, key, value)
	end
	rawgetmetatable("NpcType").__newindex = NpcTypeNewIndex
end

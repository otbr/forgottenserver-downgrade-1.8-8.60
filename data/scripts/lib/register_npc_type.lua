-- NpcType.register helper for TFS 1.8 RevScriptSys
-- Applies npcConfig table to npcType object after all event callbacks are already set.
-- Usage (last line of every NPC script):
--   npcType:register(npcConfig)

registerNpcType = {}
setmetatable(registerNpcType, {
	__call = function(self, npcType, mask)
		for _, parse in pairs(self) do
			parse(npcType, mask)
		end
	end,
})

NpcType.register = function(self, mask)
	return registerNpcType(self, mask)
end

registerNpcType.name = function(npcType, mask)
	if mask.name then
		npcType:name(mask.name)
	end
end

registerNpcType.description = function(npcType, mask)
	if mask.description then
		npcType:nameDescription(mask.description)
	end
end

registerNpcType.outfit = function(npcType, mask)
	if mask.outfit then
		npcType:outfit(mask.outfit)
	end
end

registerNpcType.health = function(npcType, mask)
	if mask.health then
		npcType:health(mask.health)
	end
end

registerNpcType.maxHealth = function(npcType, mask)
	if mask.maxHealth then
		npcType:maxHealth(mask.maxHealth)
	end
end

registerNpcType.walkInterval = function(npcType, mask)
	if mask.walkInterval then
		npcType:walkInterval(mask.walkInterval)
	end
end

registerNpcType.walkRadius = function(npcType, mask)
	if mask.walkRadius then
		npcType:walkRadius(mask.walkRadius)
	end
end

registerNpcType.speed = function(npcType, mask)
	if mask.speed then
		npcType:baseSpeed(mask.speed)
	end
end

registerNpcType.flags = function(npcType, mask)
	if mask.flags then
		if mask.flags.floorchange ~= nil then
			npcType:floorChange(mask.flags.floorchange)
		end
		if mask.flags.pushable ~= nil then
			npcType:isPushable(mask.flags.pushable)
		end
	end
end

registerNpcType.speechBubble = function(npcType, mask)
	if mask.speechBubble then
		npcType:speechBubble(mask.speechBubble)
	end
end

registerNpcType.currency = function(npcType, mask)
	if mask.currency then
		npcType:currency(mask.currency)
	end
end

-- Global item price tracker (compatible with Crystal Server NPC scripts)
NpcPriceChecker = NpcPriceChecker or {}

registerNpcType.shop = function(npcType, mask)
	if type(mask.shop) ~= "table" then
		return
	end

	local npcName = npcType:getName()

	for _, shopItems in pairs(mask.shop) do
		local itemName = shopItems.itemName or shopItems.itemname or ""
		local clientId = shopItems.clientId or shopItems.clientid or 0
		local buyPrice = shopItems.buy or 0
		local sellPrice = shopItems.sell or 0
		local subType = shopItems.subType or shopItems.subtype or shopItems.count or 1

		-- addShopItem accepts {clientId=..., buy=..., sell=..., name=..., subType=...}
		-- C++ will do the clientId -> serverId conversion automatically
		npcType:addShopItem({
			clientId = clientId,
			name = itemName,
			subType = subType,
			buy = buyPrice,
			sell = sellPrice,
		})

		-- Track prices for NpcPriceChecker
		if clientId and clientId ~= 0 then
			if not NpcPriceChecker[clientId] then
				NpcPriceChecker[clientId] = { buy = nil, sell = nil, buyNpc = nil, sellNpc = nil }
			end
			if buyPrice and buyPrice > 0 then
				NpcPriceChecker[clientId].buy = buyPrice
				NpcPriceChecker[clientId].buyNpc = npcName
			end
			if sellPrice and sellPrice > 0 then
				NpcPriceChecker[clientId].sell = sellPrice
				NpcPriceChecker[clientId].sellNpc = npcName
			end
		end
	end
end

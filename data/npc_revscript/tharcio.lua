local npcType = Game.createNpcType("Tharcio")

local npcConfig = {}
npcConfig.name        = "Tharcio"
npcConfig.description = "Tharcio"
npcConfig.health      = 100
npcConfig.maxHealth   = 100
npcConfig.walkInterval = 2000
npcConfig.walkRadius   = 2

npcConfig.outfit = {
	lookType   = 128,
	lookHead   = 40,
	lookBody   = 95,
	lookLegs   = 114,
	lookFeet   = 27,
	lookAddons = 1,
	lookMount  = 0,
}

npcConfig.flags = {
	floorchange = false,
}

-- Loja: apenas clientId + nome + preço de compra/venda
npcConfig.shop = {
	{ itemName = "health potion",        clientId = 236,  buy = 50,  sell = 20  },
	{ itemName = "mana potion",          clientId = 237,  buy = 50,  sell = 20  },
	{ itemName = "strong health potion", clientId = 238,  buy = 100, sell = 40  },
	{ itemName = "strong mana potion",   clientId = 239,  buy = 100, sell = 40  },
	{ itemName = "bread",                clientId = 3600, buy = 3              },
	{ itemName = "rope",                 clientId = 3099, buy = 50             },
	{ itemName = "shovel",               clientId = 3478, buy = 50             },
}

-- ── NpcHandler setup ────────────────────────────────────────────────────────
local keywordHandler = KeywordHandler:new()
local npcHandler     = NpcHandler:new(keywordHandler)

npcHandler:setMessage(MESSAGE_GREET,        "Olá, |PLAYERNAME|! Seja bem-vindo! Posso ajudar com {trade} ou responder suas {perguntas}.")
npcHandler:setMessage(MESSAGE_FAREWELL,     "Até logo, |PLAYERNAME|! Volte sempre.")
npcHandler:setMessage(MESSAGE_WALKAWAY,     "Cuide-se por aí!")
npcHandler:setMessage(MESSAGE_SENDTRADE,    "Claro, veja o que tenho para oferecer.")
npcHandler:setMessage(MESSAGE_ONCLOSESHOP,  "Obrigado pela visita!")
npcHandler:setMessage(MESSAGE_MISSINGMONEY, "Você não tem ouro suficiente.")
npcHandler:setMessage(MESSAGE_NEEDITEM,     "Você não tem esse item.")

-- FocusModule: registra palavras hi/bye/trade/offers automaticamente
local focus = FocusModule:new()
npcHandler:addModule(focus, npcConfig.name, true, true, true)

-- Palavra-chave de teste: "nome" → NPC responde quem é
local rootNode = keywordHandler:getRoot()
rootNode:addChildKeyword(
	{ "nome", "quem é você", "who are you" },
	StdModule.say,
	{
		npcHandler = npcHandler,
		onlyFocus  = true,
		text       = "Me chamo Thárcio, o melhor mercador desta região!",
	}
)

-- ── Eventos ─────────────────────────────────────────────────────────────────
npcType.onThink = function(npc, interval)
	npcHandler:onThink(npc, interval)
end

npcType.onAppear = function(npc, creature)
	npcHandler:onAppear(npc, creature)
end

npcType.onDisappear = function(npc, creature)
	npcHandler:onDisappear(npc, creature)
end

npcType.onMove = function(npc, creature, fromPos, toPos)
	npcHandler:onMove(npc, creature, fromPos, toPos)
end

npcType.onSay = function(npc, creature, type, message)
	npcHandler:onSay(npc, creature, type, message)
end

npcType.onCloseChannel = function(npc, creature)
	npcHandler:onCloseChannel(npc, creature)
end

-- onBuyItem: chamado quando sentinel -2 é detectado em onPlayerTrade
-- npc:sellItem entrega o item ao jogador
npcType.onBuyItem = function(npc, player, itemId, subType, amount, ignore, inBackpacks, totalCost)
	npc:sellItem(player, itemId, amount, subType, 0, ignore, inBackpacks)
end

-- onSellItem: chamado quando sentinel -3 é detectado em onPlayerTrade
npcType.onSellItem = function(npc, player, itemId, subType, amount, ignore, name, totalCost)
	player:addMoney(totalCost)
	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Você vendeu %d x %s por %d gold.", amount, name, totalCost))
end

-- ── Registro final ───────────────────────────────────────────────────────────
npcType:register(npcConfig)

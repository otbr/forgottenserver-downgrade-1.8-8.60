# Dual Wielding

O sistema de Dual Wielding permite que jogadores equipem e utilizem duas armas corpo-a-corpo simultaneamente, uma em cada mao, alternando golpes entre elas.

---

## Sumario

- [Visao Geral](#visao-geral)
- [Como Funciona](#como-funciona)
- [Configuracao](#configuracao)
- [Vocacao](#vocacao)
- [Binding por Arma](#binding-por-arma)
- [Dual Stone (Action Revscript)](#dual-stone-action-revscript)
- [Item Progressivo de Dano](#item-progressivo-de-dano)
- [Item Consumivel de Dano](#item-consumivel-de-dano)
- [Talkaction !dualwielding](#talkaction-dualwielding)
- [Mecanicas Detalhadas](#mecanicas-detalhadas)
- [Constantes e Storages](#constantes-e-storages)
- [Arquitetura Interna (C++)](#arquitetura-interna-c)

---

## Visao Geral

O sistema funciona nos seguintes modos (configuravel via `server_config.lua`):

1. **`"allweapons"`** - Qualquer arma corpo-a-corpo equipada em ambas as maos ja habilita dual wielding automaticamente.

2. **`"itemxml"`** - Apenas armas que possuem o custom attribute `dualwielding="true"` (setado via `items.xml` ou via Dual Stone) podem ser usadas em dual wielding.

Alem disso, o dano pode ser aumentado ate 100% do dano normal por hit atraves de:

- **Itens equipaveis** com custom attribute `dualWieldDamageBoost` (acumulativo entre slots)
- **Itens consumiveis** que concedem boost permanente (via storage)
- O boost total e capado em `100 - dualWieldingDamageRate`% (ex: rate base 60% → max boost 40%)

---

## Como Funciona

```
ataque do jogador
  |
  +-> getWeapon() retorna a arma da mao de ataque atual
  +-> playerWeaponCheck() aplica damageModifier = dualWieldingDamageRate + boost
  +-> onUsedWeapon() alterna a mao de ataque e controla skill advance
  |
  +-> Proximo ataque usa a outra mao
```

### Fluxo de Skill Advance

- Cada hit individual em dual wielding concede **metade** dos pontos de skill normais (1 ponto a cada 2 hits)
- A alternancia entre maos controla quais hits concedem skill

### Alternancia de Maos

```
Hit 1: mao esquerda (LEFT)  → weapon = esquerda, shield-like = direita
Hit 2: mao direita (RIGHT)   → weapon = direita, shield-like = esquerda
Hit 3: mao esquerda (LEFT)  → ...
```

---

## Configuracao

### server_config.lua

```lua
-- Dual Wielding
-- NOTE: dualWieldingSpeedRate = 200 means dual-wielding attacks twice as fast
-- dualWieldingDamageRate = 60 means each hit deals 60% of normal damage
-- dualWieldingMode = "allweapons" allows any melee weapon to dual-wield
-- dualWieldingMode = "itemxml" requires <attribute key="dualwielding" value="true"/> on weapons
allowDualWielding = false
dualWieldingSpeedRate = 200
dualWieldingDamageRate = 60
dualWieldingMode = "allweapons"
```

| Variavel | Tipo | Default | Descricao |
|----------|------|---------|-----------|
| `allowDualWielding` | boolean | `false` | Ativa/desativa o sistema globalmente |
| `dualWieldingSpeedRate` | number | `200` | Percentual de velocidade (200 = 2x mais rapido que o normal) |
| `dualWieldingDamageRate` | number | `60` | Percentual de dano base por hit (60 = 60% do dano single-wielding) |
| `dualWieldingMode` | string | `"allweapons"` | `"allweapons"` ou `"itemxml"` |

---

## Vocacao

### Vocations.xml

Para controlar quais vocacoes podem usar dual wielding, adicione o atributo `dualwield`:

```xml
<vocation id="4" clientid="1" name="Knight" ... dualwield="true">
```

| Vocacao | dualwield |
|---------|-----------|
| None (0) | false |
| Sorcerer (1) | true |
| Druid (2) | true |
| Paladin (3) | **false** |
| Knight (4) | true |
| Master Sorcerer (5) | true |
| Elder Druid (6) | true |
| Royal Paladin (7) | **false** |
| Elite Knight (8) | true |
| Monk (9) | true |
| Exalted Monk (10) | true |

> **Nota**: Paladins/Royal Paladins nao podem usar dual wielding por padrao. Para habilitar, altere `dualwield="true"` no XML.

---

## Binding por Arma

### items.xml (modo "itemxml")

Para marcar uma arma como dual-wield via XML:

```xml
<item id="661" article="a" name="fiery relic sword">
    <attribute key="dualwielding" value="true" />
    <attribute key="elementfire" value="8" />
    <attribute key="attack" value="34" />
    ...
</item>
```

> O attributo `dualwielding` e um **custom attribute** armazenado no item. No modo `"allweapons"` este atributo nao e necessario.

### Dual Stone (seta o atributo em tempo real)

No modo `"itemxml"`, use uma Dual Stone para adicionar/remover o atributo `dualwielding` de uma arma em tempo de jogo.

---

## Dual Stone (Action Revscript)

Arquivo de exemplo: `data/scripts/actions/dual_stone.lua`

```lua
local DUAL_STONE_ID = 9999 -- Substitua pelo ID real da Dual Stone

local dualStone = Action()

function dualStone.onUse(player, item, fromPosition, target, toPosition, isHotkey)
    if not target or not target:isItem() then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Use the Dual Stone on a weapon.")
        return true
    end

    local weapon = target
    local weaponType = weapon:getType():getWeaponType()

    if weaponType == WEAPON_NONE or weaponType == WEAPON_SHIELD or weaponType == WEAPON_AMMO then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "You can only use Dual Stones on weapons.")
        return true
    end

    if weaponType == WEAPON_DISTANCE or weaponType == WEAPON_WAND then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Distance and wand weapons cannot be dual wielded.")
        return true
    end

    local hasDual = weapon:getCustomAttribute("dualwielding")
    if hasDual then
        weapon:removeCustomAttribute("dualwielding")
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Dual-wielding removed from the weapon.")
    else
        weapon:setCustomAttribute("dualwielding", 1)
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Dual-wielding enabled on the weapon!")
    end

    item:remove(1)
    return true
end

dualStone:id(DUAL_STONE_ID)
dualStone:register()
```

---

## Item Progressivo de Dano

Itens equipaveis (aneis, amuletos, etc.) podem fornecer boost percentual ao dano do dual wielding via custom attribute `dualWieldDamageBoost`.

### Exemplo: Amuleto do Gladiador (Tier I)

```xml
<item id="10000" article="a" name="gladiator amulet">
    <attribute key="weight" value="500" />
    <attribute key="slotType" value="necklace" />
    <attribute key="dualWieldDamageBoost" value="10" />
    <attribute key="description" value="Increases dual wielding damage by 10%." />
</item>
```

### Exemplo: Amuleto do Gladiador (Tier IV)

```xml
<item id="10003" article="a" name="ancient gladiator amulet">
    <attribute key="weight" value="500" />
    <attribute key="slotType" value="necklace" />
    <attribute key="dualWieldDamageBoost" value="40" />
    <attribute key="description" value="Increases dual wielding damage by 40%." />
</item>
```

### Tabela de Progressao (exemplo)

| Tier | Item | Boost | Dano Total (com rate 60%) |
|------|------|-------|---------------------------|
| - | Nenhum | +0% | 60% |
| I | Gladiator Amulet | +10% | 70% |
| II | Superior Gladiator Amulet | +20% | 80% |
| III | Arcane Gladiator Amulet | +30% | 90% |
| IV | Ancient Gladiator Amulet | +40% | 100% |

> **Nota**: Multiplos itens equipados com `dualWieldDamageBoost` acumulam. O boost total e capado em `100 - dualWieldingDamageRate`.

---

## Item Consumivel de Dano

Itens consumiveis (pocoes, pergaminhos) podem conceder boost permanente ao dual wielding via storage.

### Exemplo: Potion of Dual Mastery

Arquivo de exemplo: `data/scripts/actions/dual_mastery_potion.lua`

```lua
local DUAL_MASTERY_POTION_ID = 9998 -- Substitua pelo ID real
local STORAGE_DUAL_WIELD_DAMAGE_BOOST = 50001
local BOOST_PER_USE = 5 -- +5% por uso

local dualMasteryPotion = Action()

function dualMasteryPotion.onUse(player, item, fromPosition, target, toPosition, isHotkey)
    -- Verifica se dual wielding esta ativo
    if not configManager.getBoolean(configKeys.ALLOW_DUAL_WIELDING) then
        player:sendTextMessage(MESSAGE_STATUS_SMALL, "Dual wielding is not enabled on this server.")
        return true
    end

    local currentBoost = player:getStorageValue(STORAGE_DUAL_WIELD_DAMAGE_BOOST) or 0

    -- Calcula o boost maximo permitido (100 - baseRate)
    local baseRate = configManager.getInteger(configKeys.DUAL_WIELDING_DAMAGE_RATE)
    local maxBoost = 100 - baseRate

    if currentBoost >= maxBoost then
        player:sendTextMessage(MESSAGE_STATUS_SMALL,
            string.format("Your dual wielding damage is already maxed at %d%% boost.", maxBoost))
        return true
    end

    local newBoost = math.min(currentBoost + BOOST_PER_USE, maxBoost)
    player:setStorageValue(STORAGE_DUAL_WIELD_DAMAGE_BOOST, newBoost)

    local totalDamage = baseRate + newBoost
    player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
        string.format("Dual wielding damage increased! Current: %d%% of 100%% (boost: +%d%%)",
            totalDamage, newBoost))

    player:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
    item:remove(1)
    return true
end

dualMasteryPotion:id(DUAL_MASTERY_POTION_ID)
dualMasteryPotion:register()
```

---

## Talkaction !dualwielding

Comando que exibe o status atual do dual wielding do jogador.

Arquivo de exemplo: `data/scripts/talkactions/player/dualwielding_info.lua`

```lua
local STORAGE_DUAL_WIELD_DAMAGE_BOOST = 50001

local dualInfo = TalkAction("!dualwielding")

function dualInfo.onSay(player, words, param)
    if param == "info" then
        local isEnabled = configManager.getBoolean(configKeys.ALLOW_DUAL_WIELDING)
        if not isEnabled then
            player:sendTextMessage(MESSAGE_INFO_DESCR, "Dual wielding is currently disabled on this server.")
            return false
        end

        local baseRate = configManager.getInteger(configKeys.DUAL_WIELDING_DAMAGE_RATE)
        local speedRate = configManager.getInteger(configKeys.DUAL_WIELDING_SPEED_RATE)

        -- Calcula boost de itens equipados
        local equippedBoost = 0
        for slot = CONST_SLOT_HEAD, CONST_SLOT_AMMO do
            local item = player:getSlotItem(slot)
            if item then
                local attr = item:getCustomAttribute("dualWieldDamageBoost")
                if attr then
                    equippedBoost = equippedBoost + attr
                end
            end
        end

        -- Boost permanente (storage)
        local permanentBoost = player:getStorageValue(STORAGE_DUAL_WIELD_DAMAGE_BOOST) or 0

        local totalBoost = math.min(equippedBoost + permanentBoost, 100 - baseRate)
        local totalDamage = baseRate + totalBoost

        -- Velocidade de ataque
        local baseSpeed = player:getVocation():getAttackSpeed()
        local dualSpeed = math.ceil(baseSpeed * 100.0 / speedRate)

        local mode = configManager.getString(configKeys.DUAL_WIELDING_MODE)

        player:sendTextMessage(MESSAGE_INFO_DESCR, string.format(
            "Dual Wielding Status:\n" ..
            "  Dano atual: %d%% de 100%%\n" ..
            "  Velocidade de ataque dual: %dms de %dms\n" ..
            "  Boost de itens equipados: +%d%%\n" ..
            "  Boost permanente: +%d%%",
            totalDamage, dualSpeed, baseSpeed, equippedBoost, permanentBoost
        ))
    else
        player:sendTextMessage(MESSAGE_INFO_DESCR, "Use: !dualwielding info")
    end
    return false
end

dualInfo:separator(" ")
dualInfo:register()
```

### Saida esperada

```
Dual Wielding Status:
  Dano atual: 75% de 100%
  Velocidade de ataque dual: 750ms de 1500ms
  Boost de itens equipados: +10%
  Boost permanente: +5%
```

---

## Mecanicas Detalhadas

### Requisitos para Dual Wielding ativo

1. `allowDualWielding = true` na config
2. Vocacao do jogador tem `dualwield="true"`
3. Ambas as maos (CONST_SLOT_LEFT e CONST_SLOT_RIGHT) possuem armas
4. As armas nao sao do tipo `distance`, `wand`, `shield` ou `ammo`
5. No modo `"itemxml"`: ambas as armas possuem custom attribute `dualwielding=true`

### Calculo de Velocidade

```
attackSpeed = vocationBaseSpeed × 100 / dualWieldingSpeedRate
```

Exemplo: vocation com 1500ms base × 100 / 200 = 750ms por hit

### Calculo de Dano

```
damageModifier = dualWieldingDamageRate + equippedBoost + permanentBoost
damageModifier = min(damageModifier, 100)
```

### Skill Advance

- Um ponto de skill e concedido a cada **2 hits** (um por mao)
- Isso garante que o progresso de skill seja equivalente ao single-wielding

### Defesa

- A mao que nao esta atacando e tratada como "shield" para calculo de defesa
- Alterna dinamicamente a cada hit

---

## Constantes e Storages

### Config Keys (Lua)

| Constante | Tipo | Uso |
|-----------|------|-----|
| `configKeys.ALLOW_DUAL_WIELDING` | boolean | Ativa/desativa global |
| `configKeys.DUAL_WIELDING_SPEED_RATE` | integer | % velocidade |
| `configKeys.DUAL_WIELDING_DAMAGE_RATE` | integer | % dano base |
| `configKeys.DUAL_WIELDING_MODE` | string | `"allweapons"` ou `"itemxml"` |

### Storage Keys

| Key | Uso |
|-----|-----|
| 50001 | Boost permanente de dano do dual wielding (consumiveis) |

### Custom Attributes (Items)

| Atributo | Tipo | Uso |
|----------|------|-----|
| `dualwielding` | boolean | Marca arma como dual-wield (modo `"itemxml"`) |
| `dualWieldDamageBoost` | integer | Boost percentual de dano (itens equipaveis) |

---

## Arquitetura Interna (C++)

### Classes e Enums

```
enum attackHand_t : uint8_t {
    HAND_LEFT,   // CONST_SLOT_LEFT
    HAND_RIGHT   // CONST_SLOT_RIGHT
};
```

### Player (novos membros)

```
// Campos
attackHand_t lastAttackHand = HAND_LEFT;
bool blockSkillAdvance = false;

// Metodos
bool isDualWielding() const;
void switchAttackHand();
slots_t getAttackHand() const;
void switchBlockSkillAdvance();
bool getBlockSkillAdvance() const;
int32_t getDualWieldDamageBoost() const;
```

### Fluxo de Ataque

```
Player::doAttacking()
  -> getWeapon() [usa getAttackHand() se dual wielding]
  -> Weapon::useWeapon()
    -> playerWeaponCheck() [aplica damageModifier com rate + boost]
    -> internalUseWeapon()
      -> onUsedWeapon()
        -> addSkillAdvance() [a cada 2 hits]
        -> switchBlockSkillAdvance() [alterna flag de skill]
        -> switchAttackHand() [alterna mao]
```

### ConfigManager

```
Boolean::ALLOW_DUAL_WIELDING    -> carregado de allowDualWielding
Integer::DUAL_WIELDING_SPEED_RATE   -> carregado de dualWieldingSpeedRate
Integer::DUAL_WIELDING_DAMAGE_RATE  -> carregado de dualWieldingDamageRate
String::DUAL_WIELDING_MODE      -> carregado de dualWieldingMode
```

### Vocation

```
bool dualWield = true;    // carregado do XML <vocation dualwield="true">
bool canDualWield() const;
```

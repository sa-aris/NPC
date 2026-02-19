# RPG-Inventory-Crafting-System-C-17-
A compact, single‑file C++17 demonstration of a modern RPG inventory and crafting system. It features a polymorphic item hierarchy, randomised stats via a factory, slot‑ and weight‑limited inventory, data‑driven crafting recipes, and JSON‑style serialization—all wrapped in clean, extensible code ready for integration or teaching purposes.
# 🎮 AAA‑Quality RPG Inventory & Crafting System (single‑file C++17)

A compact, **single‑file** demonstration of a modern RPG inventory and crafting system written in **C++17**.  
The code is completely self‑contained (no external libraries), making it perfect for:

* 📚 **Teaching** – show students how to combine polymorphism, factories, data‑driven design and error handling in C++.
* 🛠️ **Prototyping** – drop‑in the file into an existing project and start using a fully‑featured inventory right away.
* 🎮 **Game‑Jam** – a ready‑made, battle‑tested inventory/crafting backbone that you can extend with your own items, stats, UI, etc.

--- 

## ✨ Features at a glance

| Category | What it does |
|----------|--------------|
| **Item model** | Polymorphic `Item` using `std::variant` (Weapon, Armor, Consumable, Material, Misc). |
| **Factory** | `ItemFactory` creates items from a template pool, randomises stats based on player level and rarity. |
| **Rarity & Stats** | Five rarity tiers (Common → Legendary) with colour‑coded console output. |
| **Stacking** | Stackable materials & consumables (max 20 per slot), automatic stacking on add. |
| **Inventory limits** | Configurable slot count & total weight limit. |
| **Equipment** | Head, Chest, Legs, Weapon, Shield, Accessory slots; auto‑unequip handling. |
| **Crafting** | Data‑driven recipes (JSON), ingredient validation, automatic result creation. |
| **Persistence** | JSON‑style `serialize()` / `deserialize()` for full save‑load (items + equipment). |
| **Thread‑safe logger** | `Log::info/warn/error` with optional file output. |
| **Result<T>** | Lightweight error‑aware return type (`Result<T>` / `Result<void>`). |
| **Demo UI** | Small command‑line menu that lets you test loot, crafting, equip/unequip and save/load. |

--- 

## 📥 Getting Started

### Prerequisites
* A C++17‑compatible compiler (e.g. **g++ 9+**, **clang++ 10+**, **MSVC 19.20+**).  
* No external dependencies – the file is **stand‑alone**.

### Build & Run (Linux/macOS/WSL)

```bash
# Clone the repo
git clone https://github.com/<your‑user>/<repo‑name>.git
cd <repo‑name>

# Compile (single‑file)
g++ -std=c++17 -O2 -Wall -Wextra -pedantic main.cpp -o demo

# Run the demo
./demo
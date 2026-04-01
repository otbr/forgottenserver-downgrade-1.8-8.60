// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "otserv.h"

#include "configmanager.h"
#include "databasemanager.h"
#include "databasetasks.h"
#include "game.h"
#include "imbuement.h"
#include "outfit.h"
#include "logger.h"
#include "outputmessage.h"
#include "protocollogin.h"
#include "protocoladmin.h"
#include "protocolstatus.h"
#include "rsa.h"
#include "scheduler.h"
#include "script.h"
#include "scriptmanager.h"
#include "server.h"
#include "signals.h"
#include "luascript.h"
#include "thread_pool.h"

#include <fmt/format.h>
#include <fmt/color.h>
#include <fstream>
#if __has_include("gitmetadata.h")
#include "gitmetadata.h"
#endif

DatabaseTasks g_databaseTasks;
Dispatcher g_dispatcher;
Scheduler g_scheduler;
Stats g_stats;

Game g_game;
Monsters g_monsters;
Vocations g_vocations;

std::mutex g_loaderLock;
std::condition_variable g_loaderSignal;
std::unique_lock<std::mutex> g_loaderUniqueLock(g_loaderLock);

namespace {

void startupErrorMessage(std::string_view errorStr)
{
	LOG_ERROR(errorStr);
	g_loaderSignal.notify_all();
}

void mainLoader(ServiceManager* services)
{
	// dispatcher thread
	g_game.setGameState(GAME_STATE_STARTUP);

#ifdef STATS_ENABLED
	g_stats.setEnabled(false);
#endif

	if (!initLogger(LogLevel::INFO)) {
		startupErrorMessage("Failed to initialize logger!");
		return;
	}

	setupLoggerSignalHandlers();

	g_threadPool.start();

	srand(static_cast<unsigned int>(OTSYS_TIME()));
#ifdef _WIN32
	SetConsoleTitle(STATUS_SERVER_NAME);

	// fixes a problem with escape characters not being processed in Windows consoles
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD dwMode = 0;
	GetConsoleMode(hOut, &dwMode);
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(hOut, dwMode);
#endif

	printServerVersion();

	// Suppress console output during loading — logs still go to file
	g_logger().setConsoleLevel(LogLevel::CRITICAL);

	// check if config.lua or config.lua.dist exist
	auto configFile = getString(ConfigManager::CONFIG_FILE);
	std::ifstream c_test(fmt::format("./{}", configFile));
	if (!c_test.is_open()) {
		std::ifstream config_lua_dist("./config.lua.dist");
		if (config_lua_dist.is_open()) {
			LOG_INFO(fmt::format(">> copying config.lua.dist to {}", configFile));
			std::ofstream config_lua(std::string{configFile});
			config_lua << config_lua_dist.rdbuf();
			config_lua.close();
			config_lua_dist.close();
		}
	} else {
		c_test.close();
	}

	// read global config
	LOG_INFO(">> Loading config");
	if (!ConfigManager::load()) {
		startupErrorMessage(fmt::format("Unable to load {}!", configFile));
		return;
	}
	g_logger().setLevel(parseLogLevel(getString(ConfigManager::LOG_LEVEL)));

#ifdef _WIN32
	auto defaultPriority = getString(ConfigManager::DEFAULT_PRIORITY);
	if (caseInsensitiveEqual(defaultPriority, "high")) {
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	} else if (caseInsensitiveEqual(defaultPriority, "above-normal")) {
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	}
#endif

	// set RSA key
	try {
		std::ifstream key{"key.pem"};
		std::string pem{std::istreambuf_iterator<char>{key}, std::istreambuf_iterator<char>{}};
		tfs::rsa::loadPEM(pem);
	} catch (const std::exception& e) {
		startupErrorMessage(e.what());
		return;
	}

	LOG_INFO(">> Establishing database connection...");

	if (!Database::getInstance().connect()) {
		startupErrorMessage("Failed to connect to database.");
		return;
	}

	LOG_INFO(fmt::format(">> MySQL {}", Database::getClientVersion()));

	// run database manager
	LOG_INFO(">> Running database manager");

	if (!DatabaseManager::isDatabaseSetup()) {
		startupErrorMessage(
		    "The database you have specified in config.lua is empty, please import the schema.sql to your database.");
		return;
	}
	g_databaseTasks.start();

	DatabaseManager::updateDatabase();

	if (getBoolean(ConfigManager::OPTIMIZE_DATABASE) && !DatabaseManager::optimizeTables()) {
		LOG_INFO(">> No tables were optimized.");
	}

	// load vocations
	if (!g_vocations.loadFromXml()) {
		startupErrorMessage("Unable to load vocations!");
		return;
	}

	// instantiate required script systems for items
	if (!ScriptingManager::getInstance().loadPreItems()) {
		startupErrorMessage("Failed to initialize pre-item script systems");
		return;
	}

	// load item data
	LOG_INFO(">> Loading items... ");
	if (!Item::items.loadFromOtb("data/items/items.otb")) {
		startupErrorMessage("Unable to load items (OTB)!");
		return;
	}
	LOG_INFO(fmt::format(">> OTB v{:d}.{:d}.{:d}", Item::items.majorVersion, Item::items.minorVersion,
	                         Item::items.buildNumber));

	if (!Item::items.loadFromXml()) {
		startupErrorMessage("Unable to load items (XML)!");
		return;
	}

	LOG_INFO(">> Loading imbuements");
	if (!Imbuements::getInstance().loadFromXml()) {
		startupErrorMessage("Unable to load imbuements!");
		return;
	}

	LOG_INFO(">> Loading script systems");
	if (!ScriptingManager::getInstance().loadScriptSystems()) {
		startupErrorMessage("Failed to load script systems");
		return;
	}

	g_game.raids.getScriptInterface().initState();

	LOG_INFO(">> Loading spells");
	if (!g_scripts->loadScripts("scripts/spells", false, false)) {
		startupErrorMessage("Failed to load spell scripts");
		return;
	}

	LOG_INFO(">> Loading lua scripts");
	if (!g_scripts->loadScripts("scripts", false, false)) {
		startupErrorMessage("Failed to load lua scripts");
		return;
	}

	LOG_INFO(fmt::format(">> Loading monsters... [\033[1;33m{}\033[0m]", g_monsters.monsters.size()));
	
	LOG_INFO(">> Loading outfits");
	if (!Outfits::getInstance().loadFromXml()) {
		startupErrorMessage("Unable to load outfits!");
		return;
	}

	LOG_INFO(">> Checking world type... ");
	auto worldType = boost::algorithm::to_lower_copy<std::string>(std::string{getString(ConfigManager::WORLD_TYPE)});
	if (worldType == "pvp") {
		g_game.setWorldType(WORLD_TYPE_PVP);
	} else if (worldType == "no-pvp") {
		g_game.setWorldType(WORLD_TYPE_NO_PVP);
	} else if (worldType == "pvp-enforced") {
		g_game.setWorldType(WORLD_TYPE_PVP_ENFORCED);
	} else {
		LOG_INFO("\n");
		startupErrorMessage(
		    fmt::format("Unknown world type: {:s}, valid world types are: pvp, no-pvp and pvp-enforced.",
		                getString(ConfigManager::WORLD_TYPE)));
		return;
	}
	LOG_INFO(fmt::format(">> {}", boost::algorithm::to_upper_copy(worldType)));

	LOG_INFO(">> Loading map");
	if (!g_game.loadMainMap(std::string{getString(ConfigManager::MAP_NAME)})) {
		startupErrorMessage("Failed to load map");
		return;
	}

	LOG_INFO(">> Initializing gamestate");
	g_game.setGameState(GAME_STATE_INIT);

	// Game client protocols
	services->add<ProtocolGame>(static_cast<uint16_t>(getInteger(ConfigManager::GAME_PORT)));
	services->add<ProtocolLogin>(static_cast<uint16_t>(getInteger(ConfigManager::LOGIN_PORT)));

	// OT protocols
	services->add<ProtocolStatus>(static_cast<uint16_t>(getInteger(ConfigManager::STATUS_PORT)));
	services->add<ProtocolAdmin>(static_cast<uint16_t>(getInteger(ConfigManager::ADMIN_PORT)));

	RentPeriod_t rentPeriod;
	auto strRentPeriod =
	    boost::algorithm::to_lower_copy<std::string>(std::string{getString(ConfigManager::HOUSE_RENT_PERIOD)});

	if (strRentPeriod == "yearly") {
		rentPeriod = RENTPERIOD_YEARLY;
	} else if (strRentPeriod == "weekly") {
		rentPeriod = RENTPERIOD_WEEKLY;
	} else if (strRentPeriod == "monthly") {
		rentPeriod = RENTPERIOD_MONTHLY;
	} else if (strRentPeriod == "daily") {
		rentPeriod = RENTPERIOD_DAILY;
	} else if (strRentPeriod == "dev") {
		rentPeriod = RENTPERIOD_DEV;
	} else {
		rentPeriod = RENTPERIOD_NEVER;
	}

	g_game.map.houses.payHouses(rentPeriod);

	LOG_INFO(">> Loaded all modules, server starting up...");

#ifndef _WIN32
	if (getuid() == 0 || geteuid() == 0) {
		LOG_INFO(fmt::format("> Warning: {} has been executed as root user, please consider running it as a normal user.", STATUS_SERVER_NAME));
	}
#endif

	g_game.start(services);
	g_game.setGameState(GAME_STATE_NORMAL);

	// Pre-warm the OutputMessage pool to avoid operator new() on first connections
	OutputMessagePool::prewarmPool(128);

#ifdef STATS_ENABLED
	g_stats.setEnabled(true);
#endif

	g_loaderSignal.notify_all();
}

[[noreturn]] void badAllocationHandler()
{
	// Use functions that only use stack allocation
	puts("Allocation failed, server out of memory.\nDecrease the size of your map or compile in 64 bits mode.\n");
	getchar();
	exit(-1);
}

} // namespace

void startServer()
{
	std::set_new_handler(badAllocationHandler);

	ServiceManager serviceManager;

	g_dispatcher.start();
	g_scheduler.start();
#ifdef STATS_ENABLED
	g_stats.start();
#endif

	g_dispatcher.addTask(createTaskWithStats([=, services = &serviceManager]() { mainLoader(services); }, "MainLoader", ""));

	g_loaderSignal.wait(g_loaderUniqueLock);

	if (serviceManager.is_running()) {
		using fmt::fg;
		using fmt::emphasis;

		const auto cyan_b    = fg(fmt::color::cyan) | emphasis::bold;
		const auto green_b   = fg(fmt::color::lime_green) | emphasis::bold;
		const auto white_b   = fg(fmt::color::white) | emphasis::bold;
		const auto gray      = fg(fmt::color::gray);
		const auto dark_gray = fg(fmt::color::dim_gray);

		// ── Server Config ──
		fmt::print(cyan_b, "    ⚙  SERVER CONFIG\n");
		fmt::print(dark_gray, "    ────────────────────────────────────────\n");
		fmt::print(gray, "    {:<20}", "World Map");
		fmt::print(white_b, "{}\n", getString(ConfigManager::MAP_NAME));
		fmt::print(gray, "    {:<20}", "World Size");
		fmt::print(white_b, "{}x{}\n", g_game.map.getWidth(), g_game.map.getHeight());
		fmt::print(gray, "    {:<20}", "World Type");
		fmt::print(white_b, "{}\n", getString(ConfigManager::WORLD_TYPE));
		fmt::print(gray, "    {:<20}", "Account Manager");
		fmt::print(white_b, "{}\n", getBoolean(ConfigManager::ACCOUNT_MANAGER) ? "enabled" : "disabled");
		fmt::print(gray, "    {:<20}", "Game Port");
		fmt::print(white_b, "{} ✔\n", getInteger(ConfigManager::GAME_PORT));
		fmt::print(gray, "    {:<20}", "Login Port");
		fmt::print(white_b, "{} ✔\n", getInteger(ConfigManager::LOGIN_PORT));
		fmt::print(gray, "    {:<20}", "Status Port");
		fmt::print(white_b, "{} ✔\n", getInteger(ConfigManager::STATUS_PORT));
		fmt::print("\n");

		// ── Threads ──
		int networkThreads = std::max(1, static_cast<int>(getInteger(ConfigManager::NETWORK_THREADS)));
		fmt::print(cyan_b, "    ⚙  THREADS\n");
		fmt::print(dark_gray, "    ────────────────────────────────────────\n");
		fmt::print(gray, "    {:<20}", "Network I/O");
		fmt::print(white_b, "{}\n", networkThreads);
		fmt::print(gray, "    {:<20}", "ThreadPool Workers");
		fmt::print(white_b, "{}\n", g_threadPool.get_thread_count());
		fmt::print(gray, "    {:<20}", "Dispatcher");
		fmt::print(white_b, "1\n");
		fmt::print(gray, "    {:<20}", "Scheduler");
		fmt::print(white_b, "1\n");
		fmt::print(gray, "    {:<20}", "DB Tasks");
		fmt::print(white_b, "1\n");
		fmt::print("\n");

		// ── Game Data ──
		fmt::print(cyan_b, "    ⚙  GAME DATA\n");
		fmt::print(dark_gray, "    ────────────────────────────────────────\n");
		fmt::print(gray, "    {:<20}", "Items");
		fmt::print(white_b, "{} ✔\n", Item::items.size());
		fmt::print(gray, "    {:<20}", "Vocations");
		fmt::print(white_b, "{} ✔\n", g_vocations.getVocations().size());
		fmt::print(gray, "    {:<20}", "Outfits");
		fmt::print(white_b, "{} (M) + {} (F) ✔\n",
			Outfits::getInstance().getOutfits(PLAYERSEX_MALE).size(),
			Outfits::getInstance().getOutfits(PLAYERSEX_FEMALE).size());
		fmt::print(gray, "    {:<20}", "Npcs");
		fmt::print(white_b, "{} ✔\n", g_game.map.spawns.getNpcCount());
		fmt::print(gray, "    {:<20}", "Monsters");
		fmt::print(white_b, "{} ✔\n", g_monsters.monsters.size());
		{
			auto& imbue = Imbuements::getInstance();
			fmt::print(gray, "    {:<20}", "Imbuements");
			fmt::print(white_b, "{}, categories {}, {} definitions ✔\n",
				imbue.getBases().size(), imbue.getCategories().size(), imbue.getDefinitions().size());
		}
		fmt::print("\n");

		// ── Online ──
		fmt::print(dark_gray, "    ─────────────────────────────────────────────────────────\n");
		fmt::print(green_b, "    ◆ ");
		fmt::print(white_b, "{}", getString(ConfigManager::SERVER_NAME));
		fmt::print(gray, " — ");
		fmt::print(green_b, "SERVER ONLINE");
		fmt::print(green_b, " ◆\n");
		fmt::print(dark_gray, "    ─────────────────────────────────────────────────────────\n");
		fmt::print("\n");
		std::fflush(stdout);

		// Restore console output now that all fmt::print sections are done
		g_logger().setConsoleLevel(LogLevel::INFO);
	serviceManager.run();
	} else {
		LOG_INFO(">> No services running. The server is NOT online.");
		g_threadPool.shutdown();
		g_scheduler.shutdown();
		g_databaseTasks.shutdown();
		g_dispatcher.shutdown();
#ifdef STATS_ENABLED
		g_stats.shutdown();
#endif
	}

	// --- Shutdown Watchdog ---
	// If shutdown takes longer than 60 seconds, force terminate to prevent hanging forever.
	std::jthread watchdog([](std::stop_token st) {
		for (int i = 0; i < 60 && !st.stop_requested(); ++i) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		if (!st.stop_requested()) {
			LOG_ERROR("[Watchdog] Shutdown exceeded 60 seconds. Forcing termination.");
			std::_Exit(EXIT_FAILURE);
		}
	});

	// Shutdown ThreadPool first - async map saves need DB connection alive
	g_threadPool.shutdown();

	// Wait for all dispatcher/scheduler tasks to finish (including Game::shutdown)
	// before closing the Lua environment. NPCs and their NpcScriptInterface
	g_scheduler.join();
	g_databaseTasks.join();
	g_dispatcher.join();
#ifdef STATS_ENABLED
	g_stats.join();
#endif

	// Only now is it safe to close Lua — all NpcScriptInterface destructors
	// have already run and released their eventTableRef handles.
	LuaEnvironment::shutdown();

	// Cleanup MySQL connection and library
	Database::shutdown();
}

void printServerVersion()
{
	using fmt::fg;
	using fmt::emphasis;

	const auto purple      = fg(fmt::color::medium_purple);
	const auto cyan_b      = fg(fmt::color::cyan) | emphasis::bold;
	const auto white_b     = fg(fmt::color::white) | emphasis::bold;
	const auto gray        = fg(fmt::color::gray);
	const auto dark_gray   = fg(fmt::color::dim_gray);
	const auto green_b     = fg(fmt::color::lime_green) | emphasis::bold;
	const auto magenta_b   = fg(fmt::color::magenta) | emphasis::bold;
	const auto red_b       = fg(fmt::color::orange_red) | emphasis::bold;

	// ── ASCII Banner ──
	fmt::print("\n");
	fmt::print(purple | emphasis::bold,
		"    ████████╗███████╗███████╗    ██████╗  ██████╗ ██╗    ██╗███╗   ██╗\n");
	fmt::print(fg(fmt::color::medium_orchid),
		"    ╚══██╔══╝██╔════╝██╔════╝    ██╔══██╗██╔═══██╗██║    ██║████╗  ██║\n");
	fmt::print(fg(fmt::color::orchid),
		"       ██║   █████╗  ███████╗    ██║  ██║██║   ██║██║ █╗ ██║██╔██╗ ██║\n");
	fmt::print(fg(fmt::color::violet),
		"       ██║   ██╔══╝  ╚════██║    ██║  ██║██║   ██║██║███╗██║██║╚██╗██║\n");
	fmt::print(cyan_b,
		"       ██║   ██║     ███████║    ██████╔╝╚██████╔╝╚███╔███╔╝██║ ╚████║\n");
	fmt::print(fg(fmt::color::dark_cyan),
		"       ╚═╝   ╚═╝     ╚══════╝    ╚═════╝  ╚═════╝  ╚══╝╚══╝ ╚═╝  ╚═══╝\n");
	fmt::print("\n");

	// ── Version bar ──
	fmt::print(dark_gray, "    ─────────────────────────────────────────────────────────\n");
	fmt::print(gray, "    ◆ ");
	fmt::print(gray, "VERSION ");
	fmt::print(white_b, "{}", STATUS_SERVER_VERSION);
	fmt::print(dark_gray, "  ·  ");
	fmt::print(gray, "CLIENT ");
	fmt::print(white_b, "{}", CLIENT_VERSION_STR);
	fmt::print(dark_gray, "  ·  ");
	fmt::print(gray, "BUILD ");
#if defined(GIT_RETRIEVED_STATE) && GIT_RETRIEVED_STATE
	fmt::print(green_b, "{}", GIT_SHORT_SHA1);
#if GIT_IS_DIRTY
	fmt::print(fg(fmt::color::gold) | emphasis::bold, " DIRTY");
#endif
#else
	fmt::print(green_b, "RELEASE");
#endif
	fmt::print("\n");
	fmt::print(dark_gray, "    ─────────────────────────────────────────────────────────\n");

	// ── Build info section ──
	fmt::print(cyan_b, "\n    ⚙  BUILD INFO\n");
	fmt::print(dark_gray, "    ────────────────────────────────────────\n");
	fmt::print(gray, "    {:<20}", "Compiler");
	fmt::print(white_b, "{}\n", BOOST_COMPILER);
	fmt::print(gray, "    {:<20}", "Compiled");
	fmt::print(white_b, "{} {}\n", __DATE__, __TIME__);
	fmt::print(gray, "    {:<20}", "Platform");
#if defined(__amd64__) || defined(_M_X64)
	fmt::print(white_b, "x64\n");
#elif defined(__i386__) || defined(_M_IX86) || defined(_X86_)
	fmt::print(white_b, "x86\n");
#elif defined(__arm__)
	fmt::print(white_b, "ARM\n");
#else
	fmt::print(white_b, "unknown\n");
#endif
	fmt::print(gray, "    {:<20}", "Lua Engine");
#if defined(LUAJIT_VERSION)
	fmt::print(white_b, "{}\n", LUAJIT_VERSION);
#else
	fmt::print(white_b, "{}\n", LUA_RELEASE);
#endif
	fmt::print(gray, "    {:<20}", "CPU Threads");
	fmt::print(white_b, "{}\n", std::max(1u, std::thread::hardware_concurrency()));
	fmt::print("\n");

	// ── Credits ──
	fmt::print(dark_gray, "    ─────────────────────────────────────────────────────────\n");
	fmt::print(gray, "    ► Developed by ");
	fmt::print(white_b, "{}\n", STATUS_SERVER_DEVELOPERS);
	fmt::print(gray, "    ► Downgraded by ");
	fmt::print(magenta_b, "Nekiro / MillhioreBT\n");
	fmt::print(gray, "    ► Custom fork by ");
	fmt::print(red_b, "Mateuzkl\n");
	fmt::print(dark_gray, "    ─────────────────────────────────────────────────────────\n");
	fmt::print("\n");

	std::fflush(stdout);
}

void printCustomInfo()
{
	// Credits are now part of printServerVersion() banner
}

#ifndef _WIN32
// Called by GDB on crash — must be extern "C" and __attribute__((used)) to prevent stripping
extern "C" __attribute__((used)) void saveServer()
{
	if (g_game.getPlayersOnline() > 0) {
		g_game.saveGameState(true);
	}
}
#endif

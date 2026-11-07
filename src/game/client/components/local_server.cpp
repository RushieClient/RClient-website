#include "local_server.h"

#include <base/fs.h>
#include <base/mem.h>
#include <base/secure.h>
#include <base/str.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#if defined(CONF_PLATFORM_EMSCRIPTEN)
#include <emscripten/emscripten.h>
#elif defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#else
#include <base/process.h>
#endif

bool CLocalServer::RunServer(const std::vector<const char *> &vpArguments)
{
	secure_random_password(m_aRconPassword, sizeof(m_aRconPassword), 16);
	char aAuthCommand[64 + sizeof(m_aRconPassword)];
	str_format(aAuthCommand, sizeof(aAuthCommand), "auth_add %s admin %s", DEFAULT_SAVED_RCON_USER, m_aRconPassword);

	std::vector<const char *> vpArgumentsWithAuth = vpArguments;
	vpArgumentsWithAuth.push_back(aAuthCommand);

#if defined(CONF_PLATFORM_EMSCRIPTEN)
	EM_ASM({
		const args = [];
		for(let i = 0; i < $1; ++i)
			args.push(UTF8ToString(HEAPU32[($0 >> 2) + i]));
		Module['localServer'].start(args);
	}, vpArgumentsWithAuth.data(), vpArgumentsWithAuth.size());
	return true;
#elif defined(CONF_PLATFORM_ANDROID)
	if(StartAndroidServer(vpArgumentsWithAuth.data(), vpArgumentsWithAuth.size()))
	{
		GameClient()->m_Menus.ForceRefreshLanPage();
		return true;
	}
	else
	{
		Client()->AddWarning(SWarning(Localize("Server could not be started. Make sure to grant the notification permission in the app settings so the server can run in the background.")));
		mem_zero(m_aRconPassword, sizeof(m_aRconPassword));
		return false;
	}
#else
	char aBuf[IO_MAX_PATH_LENGTH];
	Storage()->GetBinaryPath(PLAT_SERVER_EXEC, aBuf, sizeof(aBuf));
#if defined(CONF_PLATFORM_MACOS)
	if(!fs_is_file(aBuf) && fs_parent_dir(aBuf) == 0)
	{
		str_append(aBuf, "/../../../DDNet-Server.app/Contents/MacOS/");
		str_append(aBuf, PLAT_SERVER_EXEC);
	}
#endif
	// No / in binary path means to search in $PATH, so it is expected that the file can't be opened. Just try executing anyway.
	if(str_find(aBuf, "/") == nullptr || fs_is_file(aBuf))
	{
		m_Process = process_execute(aBuf, EShellExecuteWindowState::BACKGROUND, vpArgumentsWithAuth.data(), vpArgumentsWithAuth.size());
		if(m_Process != INVALID_PROCESS)
		{
			GameClient()->m_Menus.ForceRefreshLanPage();
			return true;
		}
		else
		{
			Client()->AddWarning(SWarning(Localize("Server could not be started")));
			mem_zero(m_aRconPassword, sizeof(m_aRconPassword));
			return false;
		}
	}
	else
	{
		Client()->AddWarning(SWarning(Localize("Server executable not found, can't run server")));
		mem_zero(m_aRconPassword, sizeof(m_aRconPassword));
		return false;
	}
#endif
}

void CLocalServer::KillServer()
{
#if defined(CONF_PLATFORM_EMSCRIPTEN)
	EM_ASM({ Module['localServer'].stop(); });
#elif defined(CONF_PLATFORM_ANDROID)
	ExecuteAndroidServerCommand("shutdown");
	GameClient()->m_Menus.ForceRefreshLanPage();
#else
	if(m_Process != INVALID_PROCESS && process_kill(m_Process))
	{
		m_Process = INVALID_PROCESS;
		GameClient()->m_Menus.ForceRefreshLanPage();
	}
#endif
	mem_zero(m_aRconPassword, sizeof(m_aRconPassword));
}

bool CLocalServer::IsServerRunning()
{
#if defined(CONF_PLATFORM_EMSCRIPTEN)
	return EM_ASM_INT({ return Module['localServer'].state === 2; });
#elif defined(CONF_PLATFORM_ANDROID)
	return IsAndroidServerRunning();
#else
	if(m_Process != INVALID_PROCESS && !process_is_alive(m_Process))
	{
		KillServer();
	}
	return m_Process != INVALID_PROCESS;
#endif
}

#if defined(CONF_PLATFORM_EMSCRIPTEN)
const char *CLocalServer::ErrorString() const
{
	static char s_aError[256];
	EM_ASM({
		stringToUTF8(Module['localServer'].error, $0, $1);
	}, s_aError, sizeof(s_aError));
	return s_aError;
}
#endif

void CLocalServer::RconAuthIfPossible()
{
	if(!IsServerRunning() ||
		m_aRconPassword[0] == '\0' ||
		!net_addr_is_local(&Client()->ServerAddress()))
	{
		return;
	}
	Client()->RconAuth(DEFAULT_SAVED_RCON_USER, m_aRconPassword, g_Config.m_ClDummy);
}

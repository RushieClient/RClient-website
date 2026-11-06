#include <base/dbg.h>
#include <base/mem.h>
#include <base/net.h>
#include <base/secure.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/discord.h>

#if defined(CONF_DISCORD)
#include <engine/shared/config.h>

#define IStorage IWindowsStorage
#include <discord_game_sdk.h>
#undef IStorage

typedef enum EDiscordResult(DISCORD_API *FDiscordCreate)(DiscordVersion, struct DiscordCreateParams *, struct IDiscordCore **);

#if defined(CONF_DISCORD_DYNAMIC)
#include <dlfcn.h>
static FDiscordCreate GetDiscordCreate()
{
	void *pSdk = dlopen("discord_game_sdk.so", RTLD_NOW);
	if(!pSdk)
	{
		return nullptr;
	}
	return (FDiscordCreate)dlsym(pSdk, "DiscordCreate");
}
#else
static FDiscordCreate GetDiscordCreate()
{
	return DiscordCreate;
}
#endif

static void FormatDiscordTemplate(const char *pTemplate, char *pOutput, int OutputSize, const char *const *ppValues)
{
	const char *apTags[] = {"{server}", "{map}", "{players}", "{max_players}", "{player}", "{status}", "{team}", "{team_players}", "{max_team_players}"};
	int Length = 0;
	for(const char *p = pTemplate; *p && Length < OutputSize - 1;)
	{
		const char *pValue = p;
		int ValueLength = 1;
		int Advance = 1;
		for(int Tag = 0; Tag < 9; ++Tag)
		{
			if(str_startswith(p, apTags[Tag]))
			{
				pValue = ppValues[Tag];
				ValueLength = str_length(pValue);
				Advance = str_length(apTags[Tag]);
				break;
			}
		}
		for(int i = 0; i < ValueLength && Length < OutputSize - 1; ++i)
			pOutput[Length++] = pValue[i];
		p += Advance;
	}
	pOutput[Length] = '\0';
	str_utf8_fix_truncation(pOutput);
}

class CDiscord : public IDiscord
{
	DiscordActivity m_Activity;
	DiscordActivity m_LastSentActivity = {};
	int m_Team = -1;
	int m_TeamPlayers = 0;
	int m_MaxTeamPlayers = 0;
	bool m_UpdateActivity = false;
	int64_t m_LastActivityUpdate = 0;

	IDiscordCore *m_pCore;
	IDiscordActivityEvents m_ActivityEvents;
	IDiscordActivityManager *m_pActivityManager;

	FDiscordCreate m_pfnDiscordCreate;
	bool m_Enabled;

public:
	bool Init(FDiscordCreate pfnDiscordCreate)
	{
		m_pfnDiscordCreate = pfnDiscordCreate;
		m_Enabled = false;
		return InitDiscord();
	}
	bool InitDiscord()
	{
		if(m_pCore)
		{
			m_pCore->destroy(m_pCore);
			m_pCore = nullptr;
			m_pActivityManager = nullptr;
		}
		if(!m_Enabled)
			return false;

		mem_zero(&m_ActivityEvents, sizeof(m_ActivityEvents));

		m_ActivityEvents.on_activity_join = &CDiscord::OnActivityJoin;
		m_pActivityManager = nullptr;

		DiscordCreateParams Params;
		DiscordCreateParamsSetDefault(&Params);

		// Params.client_id = 752165779117441075; // DDNet
		// Params.client_id = 1325361453988970527; // TClient
		Params.client_id = 1397526062799388803; // R-Client
		Params.flags = EDiscordCreateFlags::DiscordCreateFlags_NoRequireDiscord;
		Params.event_data = this;
		Params.activity_events = &m_ActivityEvents;

		int Error = m_pfnDiscordCreate(DISCORD_VERSION, &Params, &m_pCore);

		if(Error != DiscordResult_Ok)
		{
			dbg_msg("discord", "error initializing discord instance, error=%d", Error);
			return true;
		}

		m_pActivityManager = m_pCore->get_activity_manager(m_pCore);

		// which application to launch when joining activity
		m_pActivityManager->register_command(m_pActivityManager, CONNECTLINK_DOUBLE_SLASH);
		m_pActivityManager->register_steam(m_pActivityManager, 412220); // steam id

		ClearGameInfo();

		return false;
	}

	void Update(bool Enabled) override
	{
		bool NeedsUpdate = m_Enabled != Enabled;
		m_Enabled = Enabled;

		if(NeedsUpdate)
			InitDiscord();

		if(m_pCore && m_Enabled)
		{
			// update every 5 seconds, rate limit is 5 updates per 20 seconds
			if(time_get() > m_LastActivityUpdate + time_freq() * 5)
			{
				m_LastActivityUpdate = time_get();
				DiscordActivity Activity = m_Activity;
				if(g_Config.m_RcDiscordCustom)
				{
					const bool Online = m_Activity.instance;
					char aPlayers[16], aMaxPlayers[16], aTeam[16], aTeamPlayers[16], aMaxTeamPlayers[16];
					str_format(aPlayers, sizeof(aPlayers), "%d", m_Activity.party.size.current_size);
					str_format(aMaxPlayers, sizeof(aMaxPlayers), "%d", m_Activity.party.size.max_size);
					str_format(aTeam, sizeof(aTeam), "%d", m_Team);
					str_format(aTeamPlayers, sizeof(aTeamPlayers), "%d", m_TeamPlayers);
					str_format(aMaxTeamPlayers, sizeof(aMaxTeamPlayers), "%d", m_MaxTeamPlayers);
					const char *apValues[] = {
						Online ? m_Activity.details : "", Online ? m_Activity.state : "",
						Online ? aPlayers : "", Online ? aMaxPlayers : "",
						Kernel()->RequestInterface<IClient>()->PlayerName(), Online ? "Online" : "Offline",
						Online && m_Team >= 0 ? aTeam : "", Online && m_Team >= 0 ? aTeamPlayers : "", Online && m_Team >= 0 ? aMaxTeamPlayers : ""};
					FormatDiscordTemplate(Online ? g_Config.m_RcDiscordDetails : g_Config.m_RcDiscordMenuDetails, Activity.details, sizeof(Activity.details), apValues);
					FormatDiscordTemplate(Online ? g_Config.m_RcDiscordState : g_Config.m_RcDiscordMenuState, Activity.state, sizeof(Activity.state), apValues);
					if(!g_Config.m_RcDiscordShowTime)
						mem_zero(&Activity.timestamps, sizeof(Activity.timestamps));
					if(g_Config.m_RcDiscordParty == 2 || (g_Config.m_RcDiscordParty == 1 && (m_Team < 0 || m_TeamPlayers <= 0 || m_MaxTeamPlayers < m_TeamPlayers)))
						mem_zero(&Activity.party.size, sizeof(Activity.party.size));
					else if(Online && g_Config.m_RcDiscordParty == 1)
					{
						Activity.party.size.current_size = m_TeamPlayers;
						Activity.party.size.max_size = m_MaxTeamPlayers;
					}
					if(!g_Config.m_RcDiscordAllowJoin)
						mem_zero(&Activity.secrets, sizeof(Activity.secrets));
				}
				if(m_UpdateActivity || mem_comp(&Activity, &m_LastSentActivity, sizeof(Activity)) != 0)
				{
					m_UpdateActivity = false;
					m_LastSentActivity = Activity;
					m_pActivityManager->update_activity(m_pActivityManager, &Activity, nullptr, nullptr);
				}
			}

			m_pCore->run_callbacks(m_pCore);
		}
	}

	void ClearGameInfo() override
	{
		mem_zero(&m_Activity, sizeof(DiscordActivity));
		m_Team = -1;
		m_TeamPlayers = 0;
		m_MaxTeamPlayers = 0;

		str_copy(m_Activity.assets.large_image, "rclient_logo");
		str_copy(m_Activity.assets.large_text, "RClient logo");
		m_Activity.timestamps.start = time_timestamp();
		str_copy(m_Activity.details, "Offline");
		m_Activity.instance = false;

		m_UpdateActivity = true;
	}

	void SetGameInfo(const CServerInfo &ServerInfo, bool Registered) override
	{
		mem_zero(&m_Activity, sizeof(DiscordActivity));
		m_Team = -1;
		m_TeamPlayers = 0;
		m_MaxTeamPlayers = 0;

		str_copy(m_Activity.assets.large_image, "rclient_logo");
		str_copy(m_Activity.assets.large_text, "RClient logo");
		m_Activity.timestamps.start = time_timestamp();
		str_copy(m_Activity.name, "Online");
		m_Activity.instance = true;

		str_copy(m_Activity.details, ServerInfo.m_aName);
		str_copy(m_Activity.state, ServerInfo.m_aMap);
		m_Activity.party.size.current_size = ServerInfo.m_NumClients;
		m_Activity.party.size.max_size = ServerInfo.m_MaxClients;
		// private makes it so the game isn't public to join, but there's 'Ask to Join' button instead
		m_Activity.party.privacy = Registered ? DiscordActivityPartyPrivacy_Public : DiscordActivityPartyPrivacy_Private;

		if(!Registered)
		{
			// private parties have random id to not leak the server ip
			char aPartyId[sizeof(m_Activity.party.id)];
			secure_random_password(aPartyId, sizeof(aPartyId), 64);
			str_copy(m_Activity.party.id, aPartyId);
		}
		UpdateServerIp(ServerInfo);

		m_UpdateActivity = true;
	}

	void UpdateServerInfo(const CServerInfo &ServerInfo) override
	{
		if(!m_Activity.instance)
			return;

		UpdateServerIp(ServerInfo);

		str_copy(m_Activity.details, ServerInfo.m_aName);
		str_copy(m_Activity.state, ServerInfo.m_aMap);
		m_Activity.party.size.max_size = ServerInfo.m_MaxClients;
		m_UpdateActivity = true;
	}

	void UpdatePlayerCount(int Count, int Team, int TeamPlayers, int MaxTeamPlayers) override
	{
		if(!m_Activity.instance)
			return;

		if(m_Activity.party.size.current_size == Count && m_Team == Team && m_TeamPlayers == TeamPlayers && m_MaxTeamPlayers == MaxTeamPlayers)
			return;

		m_Activity.party.size.current_size = Count;
		m_Team = Team;
		m_TeamPlayers = TeamPlayers;
		m_MaxTeamPlayers = MaxTeamPlayers;
		m_UpdateActivity = true;
	}

	void UpdateServerIp(const CServerInfo &ServerInfo)
	{
		if(!m_Activity.instance)
			return;

		// secret is only shared when player is joining the game, or when they are invited for private games
		if(str_length(ServerInfo.m_aAddress) < (int)sizeof(m_Activity.secrets.join))
		{
			str_copy(m_Activity.secrets.join, ServerInfo.m_aAddress);
		}
		else
		{
			char aAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&ServerInfo.m_aAddresses[0], aAddr, sizeof(aAddr), true);
			str_copy(m_Activity.secrets.join, aAddr);
		}

		if(m_Activity.party.privacy == DiscordActivityPartyPrivacy_Public)
		{
			// id is sha256, because it didn't work with the ':' character
			char aPartyId[SHA256_MAXSTRSIZE];
			SHA256_DIGEST PartyIdSha256 = sha256(m_Activity.secrets.join, str_length(m_Activity.secrets.join));
			sha256_str(PartyIdSha256, aPartyId, sizeof(aPartyId));
			str_copy(m_Activity.party.id, aPartyId);
		}
	}

	static void DISCORD_CALLBACK OnActivityJoin(void *pEventData, const char *pSecret)
	{
		CDiscord *pSelf = static_cast<CDiscord *>(pEventData);
		IClient *pClient = pSelf->Kernel()->RequestInterface<IClient>();
		pClient->Connect(pSecret);
	}

	~CDiscord() override
	{
		if(m_pCore)
			m_pCore->destroy(m_pCore);
	}
};

static IDiscord *CreateDiscordImpl()
{
	FDiscordCreate pfnDiscordCreate = GetDiscordCreate();
	if(!pfnDiscordCreate)
	{
		return nullptr;
	}
	CDiscord *pDiscord = new CDiscord();
	if(pDiscord->Init(pfnDiscordCreate))
	{
		delete pDiscord;
		return nullptr;
	}
	return pDiscord;
}
#else
static IDiscord *CreateDiscordImpl()
{
	return nullptr;
}
#endif

class CDiscordStub : public IDiscord
{
	void Update(bool Enabled) override {}
	void ClearGameInfo() override {}
	void SetGameInfo(const CServerInfo &ServerInfo, bool Registered) override {}
	void UpdateServerInfo(const CServerInfo &ServerInfo) override {}
	void UpdatePlayerCount(int Count, int Team, int TeamPlayers, int MaxTeamPlayers) override {}
};

IDiscord *CreateDiscord()
{
	IDiscord *pDiscord = CreateDiscordImpl();
	if(pDiscord)
	{
		return pDiscord;
	}
	return new CDiscordStub();
}

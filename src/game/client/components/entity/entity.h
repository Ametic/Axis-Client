#ifndef GAME_CLIENT_COMPONENTS_ENTITY_H
#define GAME_CLIENT_COMPONENTS_ENTITY_H
#include <base/system.h>
#include <engine/console.h>
#include <game/client/component.h>
#include <vector>
#include <engine/shared/protocol.h>

class CEClient : public CComponent
{
	bool m_AttemptedJoinTeam;
	bool m_JoinedTeam;

	bool m_WeaponsGot;
	bool m_GoresServer;

	// Reply to Ping
	class CLastPing
	{
	public:
		char m_aName[MAX_NAME_LENGTH] = "";
		char m_aMessage[256] = "";
		int m_Team = -1;
	};
	CLastPing m_aLastPing;

	void OnChatMessage(int ClientId, int Team, const char *pMsg);
	virtual void OnMessage(int MsgType, void *pRawMsg) override;

	int m_LastReplyId = -1;

	// Console Commands
	virtual void OnConsoleInit() override;

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

	static void ConServerRainbowSpeed(IConsole::IResult *pResult, void *pUserData);
	static void ConServerRainbowSaturation(IConsole::IResult *pResult, void *pUserData);
	static void ConServerRainbowLightness(IConsole::IResult *pResult, void *pUserData);
	static void ConServerRainbowBody(IConsole::IResult *pResult, void *pUserData);
	static void ConServerRainbowFeet(IConsole::IResult *pResult, void *pUserData);
	static void ConServerRainbowBothPlayers(IConsole::IResult *pResult, void *pUserData);

	static void ConVotekick(IConsole::IResult *pResult, void *pUserData);

	static void ConOnlineInfo(IConsole::IResult *pResult, void *pUserData);

	static void ConPlayerInfo(IConsole::IResult *pResult, void *pUserData);

	static void ConViewLink(IConsole::IResult *pResult, void *pUserData);

	static void ConSaveSkin(IConsole::IResult *pResult, void *pUserData);
	static void ConRestoreSkin(IConsole::IResult *pResult, void *pUserData);

	static void ConReplyLast(IConsole::IResult *pResult, void *pUserData);

	static void ConCrash(IConsole::IResult *pResult, void *pUserData);

	static void ConSpectateId(IConsole::IResult *pResult, void *pUserData);

	static void ConchainGoresMode(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainFastInputs(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

public:

	bool m_SentKill;
	int m_KillCount;

	void Votekick(const char *pName, const char *pReason);

	void PlayerInfo(const char *pName);

	void SaveSkin();
	void RestoreSkin();
	void OnlineInfo(bool Integrate = false);

	// Movement Notification if tabbed out
	vec2 m_LastPos = vec2(0, 0);
	void NotifyOnMove();

	// Rainbow
	void UpdateRainbow();

	int m_BothPlayers = true;

	int m_RainbowBody[2] = {true, true};
	int m_RainbowFeet[2] = {false, false};
	int m_RainbowColor[2];
	int m_RainbowSpeed = 10;
	int m_RainbowSat[2] = {200, 200};
	int m_RainbowLht[2] = {30, 30};

	// Preview
	unsigned int m_PreviewRainbowColor[2];
	int m_ShowServerSide;
	int64_t m_ServersideDelay[2];

	int getIntFromColor(float Hue, float Sat, float LhT)
	{
		int R = round(255 * Hue);
		int G = round(255 * Sat);
		int B = round(255 * LhT);
		R = (R << 16) & 0x00FF0000;
		G = (G << 8) & 0x0000FF00;
		B = B & 0x000000FF;
		return 0xFF000000 | R | G | B;
	}

	int64_t m_RainbowDelay;

	void GoresMode();

	void GoresModeSave();
	void GoresModeRestore();

	int64_t m_JoinTeam;
	void AutoJoinTeam();
	void OnConnect();

	/* Last Movement
	 *	+left
	 *	+right
	 *	+jump
	 */
	int64_t m_LastMovement;

	bool m_FirstLaunch = false;

private:
	virtual int Sizeof() const override { return sizeof(*this); }
	virtual void OnInit() override;
	virtual void OnRender() override;
	virtual void OnStateChange(int NewState, int OldState) override;
	virtual void OnNewSnapshot() override;
	virtual void OnShutdown() override;
};

#endif

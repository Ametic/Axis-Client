#ifndef GAME_CLIENT_COMPONENTS_ENTITY_ANTISPAWNBLOCK_H
#define GAME_CLIENT_COMPONENTS_ENTITY_ANTISPAWNBLOCK_H
#include <game/client/component.h>

class CAntiSpawnBlock : public CComponent
{
	int64_t m_Delay = 0;

public:
	enum States
	{
		STATE_NONE = 0,
		STATE_IN_TEAM,
		STATE_TEAM_ZERO,
	};

	int m_State;

	void Reset(int State = STATE_NONE);

	virtual int Sizeof() const override { return sizeof(*this); }
	virtual void OnRender() override;
	virtual void OnSelfDeath() override { Reset(); }
	virtual void OnStateChange(int NewState, int OldState) override;
};

#endif

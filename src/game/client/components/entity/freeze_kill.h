#ifndef GAME_CLIENT_COMPONENTS_ENTITY_FREEZEKILL_H
#define GAME_CLIENT_COMPONENTS_ENTITY_FREEZEKILL_H

#include <cstdint>
#include <game/client/component.h>

class CFreezeKill : public CComponent
{
	bool HandleClients(int ClientId);

	void ResetTimer();

	bool m_SentFreezeKill = false;
	int64_t m_LastFreeze = 0;
public:

	virtual int Sizeof() const override { return sizeof(*this); }
	virtual void OnRender() override;
	virtual void OnReset() override;

	friend class CFreezeBars;
};

#endif

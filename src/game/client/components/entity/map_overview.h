#ifndef GAME_CLIENT_COMPONENTS_ENTITY_MAPOVERVIEW_H
#define GAME_CLIENT_COMPONENTS_ENTITY_MAPOVERVIEW_H

#include <base/vmath.h>

#include <game/client/component.h>

#include <cstdint>
#include <vector>

class CMapOverview : public CComponent
{
public:
	void Reset();

	class CPoint
	{
	public:
		ivec2 m_Pos;
		int m_TileDist;
	};

	class CCoverage
	{
	public:
		bool m_Dirty = true;

		int m_MinX = 0;
		int m_MinY = 0;
		int m_MaxX = -1;
		int m_MaxY = -1;
		int m_Width = 0;
		int m_Height = 0;

		std::vector<uint8_t> m_vMask;

		void Reset(bool Dirty);
		void Rebuild(const std::vector<CPoint> &vPoints);

		const uint8_t *RowPtr(int y) const { return &m_vMask[(size_t)y * (size_t)m_Width]; }
	};

	std::vector<CPoint> m_vPoints;
	CCoverage m_Coverage;

	vec2 GetCloseSolidTile(vec2 Pos, int MaxDistance) const;
	bool AddPoint(vec2 Pos);

	virtual int Sizeof() const override { return sizeof(*this); }
	virtual void OnRender() override;
	virtual void OnStateChange(int NewState, int OldState) override;
	virtual void OnSelfDeath() override { Reset(); }
};

#endif

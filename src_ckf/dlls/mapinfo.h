#define BUYING_EVERYONE 0
#define BUYING_ONLY_CT 1
#define BUYING_ONLY_T 2
#define BUYING_NO_ONE 3

class CMapInfo : public CPointEntity
{
public:
	void Spawn(void);
	void KeyValue(KeyValueData *pkvd);

public:
	int m_iSetupCondition;
	int m_iWaitTime;
	int m_iFreezeTime;
	int m_iSetupTime;
	int m_iRoundTime;
	int m_iEndTime;
	int m_iEndAction;
	int m_iRedDominatedAction;
	int m_iBluDominatedAction;
};

typedef enum {
	SKIP = 0,
	UPDATE,
	TOGGLE,
	RESET
} CONTROL_OP;

class CCPControls : public CPointEntity
{
public:
	CCPControls(void)
	{
		m_opLockedOp = SKIP;
		m_bLockedArg = FALSE;
		m_opDisabledOp = SKIP;
		m_bDisabledArg = FALSE;
		m_opTeamOp = SKIP;
		m_iTeamArg = 0;
	}
public:
	void Spawn(void);
	void KeyValue(KeyValueData *pkvd);
	void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	CONTROL_OP	m_opLockedOp;
	BOOL		m_bLockedArg;
	CONTROL_OP	m_opDisabledOp;
	BOOL		m_bDisabledArg;
	CONTROL_OP	m_opTeamOp;
	int			m_iTeamArg;
};

class CRoundTerminator : public CPointEntity
{
public:
	CRoundTerminator(void)
	{
		m_iWinStatus = 0;
		m_iEndTime = -1;
	}
public:
	void Spawn(void);
	void KeyValue(KeyValueData *pkvd);
	void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

public:
	int m_iWinStatus;
	int m_iEndTime;
};
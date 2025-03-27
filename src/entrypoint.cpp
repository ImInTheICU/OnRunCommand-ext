#include "entrypoint.h"
#include <any>
#include "ehandle.h"
#include "gametrace.h"

//////////////////////////////////////////////////////////////
/////////////////        Core Variables        //////////////
////////////////////////////////////////////////////////////

BaseExtension g_Ext;
CUtlVector<FuncHookBase *> g_vecHooks;
CREATE_GLOBALVARS();

template <typename... Args>
std::string string_format(const std::string &format, Args... args)
{
    int size_s = snprintf(nullptr, 0, format.c_str(), args...) + 1; // Extra space for '\0'
    if (size_s <= 0)
        return "";

    size_t size = static_cast<size_t>(size_s);
    char* buf = new char[size];
    snprintf(buf, size, format.c_str(), args...);
    std::string out = std::string(buf, buf + size - 1); // We don't want the '\0' inside
    delete buf;
    return out;
}

struct touchlist_t
{
	Vector deltavelocity;
	trace_t trace;
};

class CPlayerPawnComponent
{
public:
    virtual ~CPlayerPawnComponent() = 0;

private:
    [[maybe_unused]] unsigned char __pad0008[0x28]; // 0x8
public:
    void* m_pPawn; // 0x30
};

class CCSPlayer_MovementServices: public CPlayerPawnComponent {

};

struct SubtickMove
{
	float when;
	uint64 button;

	union
	{
		bool pressed;

		struct
		{
			float analog_forward_delta;
			float analog_left_delta;
		} analogMove;
	};

	bool IsAnalogInput()
	{
		return button == 0;
	}
};


class CMoveDataBase
{
public:
	CMoveDataBase() = default;
	CMoveDataBase(const CMoveDataBase &source)
		// clang-format off
		: m_bFirstRunOfFunctions {source.m_bFirstRunOfFunctions},
		m_bHasZeroFrametime {source.m_bHasZeroFrametime},
		m_bIsLateCommand {source.m_bIsLateCommand}, 
		m_nPlayerHandle {source.m_nPlayerHandle},
		m_vecAbsViewAngles {source.m_vecAbsViewAngles},
		m_vecViewAngles {source.m_vecViewAngles},
		m_vecLastMovementImpulses {source.m_vecLastMovementImpulses},
		m_flForwardMove {source.m_flForwardMove}, 
		m_flSideMove {source.m_flSideMove}, 
		m_flUpMove {source.m_flUpMove},
		m_vecVelocity {source.m_vecVelocity}, 
		m_vecAngles {source.m_vecAngles},
		m_bHasSubtickInputs {source.m_bHasSubtickInputs},
		unknown {source.unknown},
		m_collisionNormal {source.m_collisionNormal},
		m_groundNormal {source.m_groundNormal}, 
		m_vecAbsOrigin {source.m_vecAbsOrigin},
		m_nTickCount {source.m_nTickCount},
		m_nTargetTick {source.m_nTargetTick},
		m_flSubtickStartFraction {source.m_flSubtickStartFraction},
		m_flSubtickEndFraction {source.m_flSubtickEndFraction}
	// clang-format on
	{
		for (int i = 0; i < source.m_AttackSubtickMoves.Count(); i++)
		{
			this->m_AttackSubtickMoves.AddToTail(source.m_AttackSubtickMoves[i]);
		}
		for (int i = 0; i < source.m_SubtickMoves.Count(); i++)
		{
			this->m_SubtickMoves.AddToTail(source.m_SubtickMoves[i]);
		}
		for (int i = 0; i < source.m_TouchList.Count(); i++)
		{
			auto touch = this->m_TouchList.AddToTailGetPtr();
			touch->deltavelocity = m_TouchList[i].deltavelocity;
			touch->trace.m_pSurfaceProperties = m_TouchList[i].trace.m_pSurfaceProperties;
			touch->trace.m_pEnt = m_TouchList[i].trace.m_pEnt;
			touch->trace.m_pHitbox = m_TouchList[i].trace.m_pHitbox;
			touch->trace.m_hBody = m_TouchList[i].trace.m_hBody;
			touch->trace.m_hShape = m_TouchList[i].trace.m_hShape;
			touch->trace.m_nContents = m_TouchList[i].trace.m_nContents;
			touch->trace.m_BodyTransform = m_TouchList[i].trace.m_BodyTransform;
			touch->trace.m_vHitNormal = m_TouchList[i].trace.m_vHitNormal;
			touch->trace.m_vHitPoint = m_TouchList[i].trace.m_vHitPoint;
			touch->trace.m_flHitOffset = m_TouchList[i].trace.m_flHitOffset;
			touch->trace.m_flFraction = m_TouchList[i].trace.m_flFraction;
			touch->trace.m_nTriangle = m_TouchList[i].trace.m_nTriangle;
			touch->trace.m_nHitboxBoneIndex = m_TouchList[i].trace.m_nHitboxBoneIndex;
			touch->trace.m_eRayType = m_TouchList[i].trace.m_eRayType;
			touch->trace.m_bStartInSolid = m_TouchList[i].trace.m_bStartInSolid;
			touch->trace.m_bExactHitPoint = m_TouchList[i].trace.m_bExactHitPoint;
		}
	}

public:
	bool m_bFirstRunOfFunctions: 1;
	bool m_bHasZeroFrametime: 1;
	bool m_bIsLateCommand: 1;
	CHandle<CEntityInstance> m_nPlayerHandle;
	QAngle m_vecAbsViewAngles;
	QAngle m_vecViewAngles;
	Vector m_vecLastMovementImpulses;
	float m_flForwardMove;
	float m_flSideMove; // Warning! Flipped compared to CS:GO, moving right gives negative value
	float m_flUpMove;
	Vector m_vecVelocity;
	QAngle m_vecAngles;
	CUtlVector<SubtickMove> m_SubtickMoves;
	CUtlVector<SubtickMove> m_AttackSubtickMoves;
	bool m_bHasSubtickInputs;
	float unknown; // Set to 1.0 during SetupMove, never change during gameplay. Is apparently used for weapon services stuff.
	CUtlVector<touchlist_t> m_TouchList;
	Vector m_collisionNormal;
	Vector m_groundNormal; // unsure
	Vector m_vecAbsOrigin;
	int32_t m_nTickCount;
	int32_t m_nTargetTick;
	float m_flSubtickStartFraction;
	float m_flSubtickEndFraction;
};

class CMoveData : public CMoveDataBase
{
public:
	Vector m_outWishVel;
	QAngle m_vecOldAngles;
	float m_flMaxSpeed;
	float m_flClientMaxSpeed;
	float m_flFrictionDecel; // Related to ground acceleration subtick stuff with sv_stopspeed and friction
	bool m_bInAir;
	bool m_bGameCodeMovedPlayer; // true if usercmd cmd number == (m_nGameCodeHasMovedPlayerAfterCommand + 1)
};

void Hook_ProcessMovement(CCSPlayer_MovementServices* mv, CMoveData* md);
FuncHook<decltype(Hook_ProcessMovement)> THook_ProcessMovement(Hook_ProcessMovement, "CCSPlayer_MovementServices_ProcessUserCmd");

void Hook_ProcessMovement(CCSPlayer_MovementServices* mv, CMoveData* md)
{
	std::any val = md->m_vecViewAngles;
	TriggerEvent("movement.ext", "OnPlayerRunCommand", {string_format("%p", mv->m_pPawn), "CCSPlayerPawnBase", md->m_vecViewAngles.x, md->m_vecViewAngles.y, md->m_vecViewAngles.z}, val);
	md->m_vecViewAngles = std::any_cast<QAngle>(val);
	THook_ProcessMovement(mv, md);
}

//////////////////////////////////////////////////////////////
/////////////////          Core Class          //////////////
////////////////////////////////////////////////////////////

EXT_EXPOSE(g_Ext);
bool BaseExtension::Load(std::string& error, SourceHook::ISourceHook *SHPtr, ISmmAPI* ismm, bool late)
{
    SAVE_GLOBALVARS();
    if(!InitializeHooks()) {
        error = "Failed to initialize hooks.";
        return false;
    }

    ismm->ConPrint("Printing a text from extensions land!\n");
    return true;
}

bool BaseExtension::Unload(std::string& error)
{
    UnloadHooks();
    return true;
}

void BaseExtension::AllExtensionsLoaded()
{

}

void BaseExtension::AllPluginsLoaded()
{

}

bool BaseExtension::OnPluginLoad(std::string pluginName, void* pluginState, PluginKind_t kind, std::string& error)
{
    return true;
}

bool BaseExtension::OnPluginUnload(std::string pluginName, void* pluginState, PluginKind_t kind, std::string& error)
{
    return true;
}

const char* BaseExtension::GetAuthor()
{
    return "Swiftly Development Team";
}

const char* BaseExtension::GetName()
{
    return "OnPlayerRunCommand";
}

const char* BaseExtension::GetVersion()
{
#ifndef VERSION
    return "Local";
#else
    return VERSION;
#endif
}

const char* BaseExtension::GetWebsite()
{
    return "https://swiftlycs2.net/";
}

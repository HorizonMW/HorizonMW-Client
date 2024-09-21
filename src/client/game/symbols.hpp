#pragma once

#define WEAK __declspec(selectany)

namespace game
{
	/***************************************************************
	 * Functions
	 **************************************************************/
	

	WEAK symbol<void(int a, int b, game::playerState_s c)> BG_AddPredictableEventToPlayerstate{ 0x2C0F00 };

	WEAK symbol<void(int localClientNum, int controllerIndex, const char* text)> Cbuf_AddText{0x1CF480};
	WEAK symbol<void(const char* text_in, int limit)> Cmd_TokenizeStringWithLimit{0x157A40};
	WEAK symbol<int(game::playerState_s* ps)> BG_GetMaxSprintTime{ 0x2C3130 };
	WEAK symbol<int(Weapon weapon, bool isAlternate)> BG_SegmentedReload{ 0x2EA990 };
	WEAK symbol<int(playerState_s* ps, int hand)> PM_Weapon_AllowReload{ 0x2D85B0 };
	WEAK symbol<__int64(__int64 cgameBlob, __int64 base, unsigned int weaponID, __int64 weapDef, DWORD* color, unsigned int alignment, int hand)> DrawClipAmmoGeneric{ 0x2F20B0 };
	WEAK symbol<__int64(__int64 cgameBlob, __int64 base, unsigned int weaponID, __int64 weapDef, DWORD* color)> DrawClipAmmoType1 { 0x2F1A40 };
	WEAK symbol<ScreenPlacement*()> ScrPlace_GetActivePlacement { 0x362810 };
	WEAK symbol<float (ScreenPlacement* a1, float a2, int alignment)> ScrPlace_ApplyYWithoutSplitScreenScaling{ 0x362380 };
	WEAK symbol<float (ScreenPlacement* a1, float a2, int alignment)> ScrPlace_ApplyXWithoutSplitScreenScaling{ 0x362230 };
	WEAK symbol<__int64(unsigned int a1, char a2)> BG_ClipForWeapon { 0x2DE580 };
	WEAK symbol<void(void* cgameGlob, float* a2, unsigned int a3, /* unsigned __int8 */ int a4, int hand)> AmmoColor{0x2EFB90};
	WEAK symbol<int(__int64 cgameGlob, unsigned int weapon_id, bool is_alternate)> BG_GetClipSize { 0x2E5E40 };
	WEAK symbol<__int64(__int64 ddlContext, __int64, bool, int)> DDL_WriteAsText{0x7946F0};
	WEAK symbol<bool(DDLContext* ddlContext, int controller, int, int statsGroup)> CL_PlayerData_GetDDLBuffer{0x6FC50};
	WEAK symbol<bool(int localClient, ScreenPlacement* scrPlace, vec3_t& WorldLocation, vec2_t& Screen)> CG_WorldPosToScreenPosReal{ 0x307AD0 };
	WEAK symbol<int(const char* perkName)> BG_GetPerkCodeIndexForName{ 0x2C6270 };
	WEAK symbol<int(playerState_s* ps)> BG_PlayerLastWeaponHand{ 0x2E95F0 };
	WEAK symbol<int(Weapon weapIdx, bool isAlternate, bool isDualWielding)> BG_SprintInTime{ 0x2EB5C0 };
	WEAK symbol<int(Weapon weapIdx, bool isAlternate, bool isDualWielding)> BG_SprintOutTime{ 0x2EB640 };
	WEAK symbol<void(playerState_s* ps, PlayerHandIndex hand)> PM_SetReloadingState{ 0x2D66D0 };

	WEAK symbol<GfxScene> scene{ 0x33E4290 };
	WEAK symbol<void(int a1)> CL_VirtualLobbyShutdown{ 0x13C9C0 };
	WEAK symbol<void()> Com_ReloadVirtualLobbyFastFiles{ 0x17CF60 };
	WEAK symbol<void(const char* a1)> Com_ShutdownInternal{ 0x5543A0 };
	WEAK symbol<void()> Com_ErrorCleanup { 0x159A30 };
	WEAK symbol<void()> MemFile_Cleanup { 0x5A2B90 };
	WEAK symbol<void()> SND_ErrorCleanup { 0x700700 };
	WEAK symbol<void()> Com_ResetParseSessions{ 0x5AE7A0 };
	WEAK symbol<void()> Migration_Shutdown { 0x4EB170 };
	WEAK symbol<void()> CL_ShutdownMemory { 0x132490 };

	WEAK symbol<unsigned __int64(pathnode_t* node, float* pos)> WorldifyPosFromParent{ 0x3FD7D0 };
	WEAK symbol<int> db_hashTable{ 0x3C54FC0 };
	WEAK symbol<XAssetEntry> g_assetEntryPool{ 0x5A6E3B0 };
	WEAK symbol<unsigned int> g_zoneCount{ 0x3D10088 };

	WEAK symbol<__int64(int handle)> Com_GetClientDObj { 0x4E98E0 };
	WEAK symbol<float*(__int64 handle, __int64 dObj, short tagName)> CG_DObjGetWorldTagPos { 0xF3A90 };
	WEAK symbol<__int64(int controllerIndex, int clientNumber)> CG_GetCentity { 0x6BFF0 };

	WEAK symbol<const char* (const char* envmod)> GameInfo_GetCurrentMapCustom{ 0x4DD810 };
	WEAK symbol<__int64(const char* envmod)> GetEnvModIndex{ 0x63F80 };
	WEAK symbol</*const*/ char* (StringTable* table, int column, char* value, char column_to_return)> StringTable_Lookup{0x5A0B10};

	WEAK symbol<void(const vec3_t* angles, vec3_t* forward, vec3_t* right, vec3_t* up)> AngleVectors{ 0x59C600 };

	WEAK symbol<void(ScreenPlacement* scrPlace, float* x, float* y, float* w, float* h, int horzAlign, int vertAlign)> ScrPlace_ApplyRect{ 0x361D20 };
	WEAK symbol<__int64(__int64 ps, unsigned int weaponId, bool isAlternate, int hand)> BG_GetAmmoInClip{ 0xE9640 };
	WEAK symbol<void(DWORD weapon, bool unk, int* projectileSpeed, int* projectileSpeedUp)> BG_GetProjectileSpeed{ 0x2E6FF0 };
	WEAK symbol<void(void* a1, vec3_t* a2)> Trajectory_GetTrBase{ 0x5B0C50 };
	WEAK symbol<void(void* a1, vec3_t* a2)> Trajectory_SetTrBase{ 0x5B0CF0 };

	WEAK symbol<void(trace_t* results, const float* start, const float* end, Bounds* bounds, int passEntityNum, int contentMask)> CG_TraceCapsule{ 0x33A5A0 };
	WEAK symbol<fake_entlist*(__int64 client)> sub_14045E280{ 0x45E280 };
	WEAK symbol<void(ScreenPlacement const*, char const*, int, Font_s*, float, float, int, int, float, float const*, int)> UI_DrawText{ 0x1DC780 };

	WEAK symbol<int(msg_t* msg)> MSG_ReadByte{ 0x4EB510 };
	WEAK symbol<void(msg_t* msg, void* data, int len)> MSG_ReadData{ 0x4EB570 };
	WEAK symbol<int(msg_t* msg)> MSG_ReadShort{ 0x4EB890 };
	WEAK symbol<char* (msg_t* msg, char* buffer, unsigned int max_chars)> MSG_ReadStringLine{ 0x4EB9D0 };
	WEAK symbol<void(msg_t* buf, unsigned char* data, int length)> MSG_Init{ 0x4EB1D0 };
	WEAK symbol<void(msg_t* buf, const char* data)> MSG_WriteString{ 0x4EC050 };
	WEAK symbol<void(msg_t* buf, short data)> MSG_WriteShort { 0x4EC020 };
	WEAK symbol<void(msg_t* buf, byte data)> MSG_WriteByte { 0x4EBF30 };
	WEAK symbol<void(msg_t* msg, long value)> MSG_WriteLong{ 0x4EBFF0 };
	WEAK symbol<void(msg_t* buf, const void* data, int length)> MSG_WriteData { 0x4EBF50 };

	WEAK symbol<__int64(int controller)> LiveStorage_GetActiveStatsSource { 0x19C7B0 };
	WEAK symbol<unsigned int(const char* omnvar)> Omnvar_GetIndexByName{ 0x5A44A0 };
	WEAK symbol<OmnvarDef*(unsigned int omnvarIndex)> Omnvar_GetDef{ 0x5A4460 };
	WEAK symbol<OmnvarData*(unsigned int controllerIndex, unsigned int omnvarIndex)> CG_Omnvar_GetData{ 0x5A4250 };
	WEAK symbol<Material*(int weaponId, bool isAltWeapon)> BG_KillIcon { 0x2E8D40 };
	WEAK symbol<bool(int weaponId, bool isAltWeapon)> BG_FlipKillIcon { 0x2E5440 };
	WEAK symbol<void(__int64, const char*, unsigned int)> hksi_lua_pushlstring{ 0x79800 };
	WEAK symbol<int*(int, int)> CL_GetClientStateFromCurrentSnapshot{ 0x344320 };
	WEAK symbol<bool()> CL_IsLocalClientInGame{ 0x135890 };

	WEAK symbol<void(int type, VariableUnion u)> AddRefToValue{0x5090E0};
	WEAK symbol<void(int type, VariableUnion u)> RemoveRefToValue{0x50ABF0};
	WEAK symbol<void(unsigned int id)> AddRefToObject{0x5090D0};
	WEAK symbol<void(unsigned int id)> RemoveRefToObject{0x50AAE0};
	WEAK symbol<unsigned int(unsigned int id)> AllocThread{0x509440};
	WEAK symbol<ObjectVariableValue*(unsigned int* id)> AllocVariable{0x5094A0};

	WEAK symbol<void(int localClientNum, int controllerIndex, const char* buffer,
		void (int, int, const char*))> Cbuf_ExecuteBufferInternal{0x155BC0};
	WEAK symbol<char*(int start)> ConcatArgs{0x413050};
	WEAK symbol<void(int localClientNum, int controllerIndex, const char* text)> Cmd_ExecuteSingleCommand{0x156E90};
	WEAK symbol<void(const char* cmdName, void(), cmd_function_s* allocedCmd)> Cmd_AddCommandInternal{0x156880};
	WEAK symbol<void(const char*)> Cmd_RemoveCommand{0x157690};

	WEAK symbol<void(void*, void*)> AimAssist_AddToTargetList{0xE66C0};

	WEAK symbol<void(unsigned int weapon, bool isAlternate, 
		char* output, unsigned int maxStringLen)> BG_GetWeaponNameComplete{0x2E2500};

	WEAK symbol<CodPlayMode()> Com_GetCurrentCoDPlayMode{0x5AEF80};
	WEAK symbol<bool()> Com_InFrontend{0x76A40};
	WEAK symbol<void(float, float, int)> Com_SetSlowMotion{0x17E5F0};
	WEAK symbol<void(errorParm code, const char* message, ...)> Com_Error{0x159860};
	WEAK symbol<void()> Com_Quit_f{0x1F9280};
	WEAK symbol<void(char const* finalMessage)> Com_Shutdown{0x157E10};
	WEAK symbol<bool(const char* mapname, const char** base_mapname)> Com_IsAddonMap{0x17C100};
	WEAK symbol<int(char* dest, int size, const char* fmt, ...)> Com_sprintf{0x5AF0F0};

	WEAK symbol<void()> Quit{0x17CF50};

	WEAK symbol<void(int localClientNum, const char* message)> CG_GameMessage{0x316210};
	WEAK symbol<void(int localClientNum, const char* message)> CG_GameMessageBold{0x3122F0};
	WEAK symbol<char*(const unsigned int weapon, 
		bool isAlternate, char* outputBuffer, int bufferLen)> CG_GetWeaponDisplayName{0x104B70};

	WEAK symbol<bool()> CL_IsCgameInitialized{0x33C640};
	WEAK symbol<const char* (int configStringIndex)> CL_GetConfigString{0x33B820};

	WEAK symbol<void(int a1)> CL_ShowSystemCursor{0x5BAA60};
	WEAK symbol<void(tagPOINT* position)> CL_GetCursorPos{0x5BA800};

	WEAK symbol<void(int hash, const char* name, const char* buffer)> Dvar_SetCommand{0x1857D0};
	WEAK symbol<dvar_t*(const char* name)> Dvar_FindVar{0x183EB0};
	WEAK symbol<dvar_t*(int hash)> Dvar_FindMalleableVar{0x183E20};
	WEAK symbol<void(const dvar_t* dvar)> Dvar_ClearModified{0x183D50};
	WEAK symbol<void(char* buffer, int index)> Dvar_GetCombinedString{0x4EA020};
	WEAK symbol<const char*(dvar_t* dvar, bool is_hashed, dvar_value value)> Dvar_ValueToString{0x187000};
	WEAK symbol<void(dvar_t* dvar, DvarSetSource source)> Dvar_Reset{0x185390};
	WEAK symbol<void(const char*, const char*, 
		DvarSetSource)> Dvar_SetFromStringByNameFromSource{0x185BD0};
	WEAK symbol<void(dvar_t* dvar, const char* string, DvarSetSource source)> Dvar_SetFromStringFromSource{0x185C60};
	WEAK symbol<void(dvar_t* dvar, dvar_value* value, DvarSetSource source)> Dvar_SetVariant{0x186120};
	WEAK symbol<char*(int bit)> Dvar_InfoString_Big{0x4EA540};

	WEAK symbol<dvar_t*(int hash, const char* name, bool value, 
		unsigned int flags)> Dvar_RegisterBool{0x182340};
	WEAK symbol<dvar_t*(int hash, const char* name, int value, int min, int max, 
		unsigned int flags)> Dvar_RegisterInt{0x182A10};
	WEAK symbol<dvar_t*(int hash, const char* dvarName, float value, float min, 
		float max, unsigned int flags)> Dvar_RegisterFloat{0x1827F0};
	WEAK symbol<dvar_t*(int hash, const char* dvarName, float value, float min, 
		float max, unsigned int flags)> Dvar_RegisterFloatHashed{0x182900};
	WEAK symbol<dvar_t*(int hash, const char* dvarName, const char* value, 
		unsigned int flags)> Dvar_RegisterString{0x182BD0};
	WEAK symbol<dvar_t*(int dvarName, const char* a2, float x, float y, float z, 
		float min, float max, unsigned int flags)> Dvar_RegisterVec3{0x182DB0};
	WEAK symbol<dvar_t*(int dvarName, const char* a2, float x, float y, float z, 
		float w, float min, float max, unsigned int flags)> Dvar_RegisterVec4{0x183010};

	WEAK symbol<long long(const char* qpath, char** buffer)> FS_ReadFile{0x1EC690};
	WEAK symbol<void(void* buffer)> FS_FreeFile{0x59E2F0};
	WEAK symbol<void(const char* path, const char* dir)> FS_AddLocalizedGameDirectory{0x1878F0};

	WEAK symbol<unsigned int(unsigned int)> GetObjectType{0x50A810};
	WEAK symbol<unsigned int(unsigned int, unsigned int)> GetVariable{0x50A8D0};
	WEAK symbol<unsigned int(unsigned int parentId, unsigned int unsignedValue)> GetNewVariable{0x50A4F0};
	WEAK symbol<unsigned int(unsigned int parentId, unsigned int unsignedValue)> GetNewArrayVariable{0x50A370};
	WEAK symbol<unsigned int(unsigned int parentId, unsigned int name)> FindVariable{0x509F90};
	WEAK symbol<unsigned int(int entnum, unsigned int classnum)> FindEntityId{0x509E90};
	WEAK symbol<void(unsigned int parentId, unsigned int index)> RemoveVariableValue{0x50AC90};
	WEAK symbol<void(VariableValue* result, unsigned int classnum, 
		int entnum, int offset)> GetEntityFieldValue{0x50E2E0};

	WEAK symbol<int(const char* fname)> generateHashValue{0x183F80};

	WEAK symbol<void()> G_Glass_Update{0x417940};
	WEAK symbol<int(int clientNum)> G_GetClientScore{0x420420};
	WEAK symbol<unsigned int(const char* name)> G_GetWeaponForName{0x461180};
	WEAK symbol<int(playerState_s* ps, unsigned int weapon, int dualWield, 
		int startInAltMode, int, int, int, char, ...)> G_GivePlayerWeapon{0x461600};
	WEAK symbol<void(playerState_s* ps, unsigned int weapon, int hadWeapon)> G_InitializeAmmo{0x41C170};
	WEAK symbol<void(const char* fmt, ...)> G_LogPrintf{0x4215C0};
	WEAK symbol<void(int clientNum, unsigned int weapon)> G_SelectWeapon{0x462560};
	WEAK symbol<int(const char* buffer, int entity, int type)> G_SetFog{0x413160};
	WEAK symbol<int(playerState_s* ps, unsigned int weapon)> G_TakePlayerWeapon{0x462770};

	WEAK symbol<int(const char* buf, int max, char** infos)> GameInfo_ParseArenas{0x4DE0B0};

	WEAK symbol<char*(const size_t size)> Hunk_AllocateTempMemoryHigh{0x59DEC0};

	WEAK symbol<char*(char* string)> I_CleanStr{0x5AF2E0};

	WEAK symbol<const char*(int, int, int)> Key_KeynumToString{0x199990};
	WEAK symbol<int(const char* cmd)> Key_GetBindingForCmd{0x1572B0};
	WEAK symbol<void(int local_client_num, int keynum, int binding)> Key_SetBinding{0x199AE0};

	WEAK symbol<unsigned int(int)> Live_SyncOnlineDataFlags{0x1A5C10};

	WEAK symbol<Material*(const char* material)> Material_RegisterHandle{0x692360};

	// NET
	WEAK symbol<void(netadr_s*, sockaddr*)> NetadrToSockadr{0x59E580};
	WEAK symbol<void(netsrc_t sock, netadr_s* addr, const char*)> NET_OutOfBandPrint{0x4F1EB0};
	WEAK symbol<int(netsrc_t sock, netadr_s* addr, char* data, int len)> NET_OutOfBandVoiceData{0x4F1FA0};
	WEAK symbol<void(netsrc_t sock, int length, const void* data, const netadr_s* to)> NET_SendLoopPacket{0x4F2070};
	WEAK symbol<bool(const char* s, netadr_s* a)> NET_StringToAdr{0x4F2150};
	WEAK symbol<bool(netadr_s a, char* s, int length)> NET_AdrToString{0x7F5DF0};

	WEAK symbol<__int64(float x, float y, float width, float height, float s0, float t0, float s1, float t1,
		float* color, Material* material)> R_AddCmdDrawStretchPic{0x33B2B0};
	WEAK symbol<void(ScreenPlacement* scrPlace, float x, float y, float w, float h, float angle, float* color, Material* material)> CG_DrawRotatedPicPhysical{ 0x3057C0 };

	WEAK symbol<int64_t(ID3D11Buffer**)> R_DestroyComputeBuffers{0xAA5B0};
	WEAK symbol<Font_s*(const char* font, int size)> R_RegisterFont{0x67F630};
	WEAK symbol<int(const char* text, int maxChars, Font_s* font)> R_TextWidth{0x67F8F0};
	WEAK symbol<int(void* font)> R_GetFontHeight{0x67F710};
	WEAK symbol<void*(int a1)> R_GetSomething{0x67ECE0};
	WEAK symbol<void()> R_SyncRenderThread{0x6A78C0};
	WEAK symbol<void*(const char* text, int maxChars, void* font, int fontHeight, float x, 
		float y, float xScale, float yScale, float rotation, float* color, 
		int style, int cursor_pos, char cursor_char, 
		void* style_unk)> H1_AddBaseDrawTextCmd{0x6A3080};

#define R_AddCmdDrawText(TXT, MC, F, X, Y, XS, YS, R, C, S) \
	H1_AddBaseDrawTextCmd(TXT, MC, F, game::R_GetFontHeight(F), X, Y, XS, YS, R, C, S, -1, 0, game::R_GetSomething(S))
#define R_AddCmdDrawTextWithCursor(TXT, MC, F, UNK, X, Y, XS, YS, R, C, S, CP, CC) \
	H1_AddBaseDrawTextCmd(TXT, MC, F, game::R_GetFontHeight(F), X, Y, XS, YS, R, C, S, CP, CC, game::R_GetSomething(S))

	WEAK symbol<int()> R_PopRemoteScreenUpdate{0x6A6D60};
	WEAK symbol<void(int)> R_PushRemoteScreenUpdate{0x6A6E60};

	WEAK symbol<char* (GfxImage* image, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipCount,
		uint32_t imageFlags, DXGI_FORMAT imageFormat, const char* name, const D3D11_SUBRESOURCE_DATA* initData)> Image_Setup{0x683890};

	WEAK symbol<char*(const size_t size, unsigned int alignment, 
		unsigned int type, PMem_Source source)> PMem_AllocFromSource_NoDebug{0x5A4DC0};
	WEAK symbol<void(char* buf, const size_t size, unsigned int alignment, 
		unsigned int type, PMem_Source source)> PMem_PopFromSource_NoDebug{0x5A5700};

	WEAK symbol<unsigned int(unsigned int localId, const char* pos, 
		unsigned int paramcount)> VM_Execute{0x510EB0};

	WEAK symbol<void(const char* value)> Scr_AddString{0x50EC50};
	WEAK symbol<void(int value)> Scr_AddInt{0x50EB70};
	WEAK symbol<void(int value)> Scr_AddBool{0x50E8F0};

	WEAK symbol<void(unsigned int id, scr_string_t stringValue, 
		unsigned int paramcount)> Scr_NotifyId{0x510340};
	WEAK symbol<const float*(const float* v)> Scr_AllocVector{0x50B330};
	WEAK symbol<float(int index)> Scr_GetFloat{0x50F870};
	WEAK symbol<const char*(int index)> Scr_GetString{0x50FCB0};
	WEAK symbol<int()> Scr_GetNumParam{0x50F9D0};
	WEAK symbol<bool(VariableValue* value)> Scr_CastString{0x50B4B0};
	WEAK symbol<void()> Scr_ClearOutParams{0x50F070};
	WEAK symbol<scr_entref_t(unsigned int entId)> Scr_GetEntityIdRef{0x50D8E0};
	WEAK symbol<unsigned int(int classnum, unsigned int entnum)> Scr_GetEntityId{0x50D830};
	WEAK symbol<int(unsigned int classnum, int entnum, int offset)> Scr_SetObjectField{0x459CD0};
	WEAK symbol<void()> Scr_ErrorInternal{0x50F0D0};

	WEAK symbol<unsigned int(const char* filename)> Scr_LoadScript{0x504FA0};
	WEAK symbol<unsigned int(const char* filename, unsigned int handle)> Scr_GetFunctionHandle{0x504E30};
	WEAK symbol<unsigned int(int handle, int num_param)> Scr_ExecThread{0x50F150};
	WEAK symbol<unsigned int(void* func, int type, unsigned int name)> Scr_RegisterFunction{0x504840};

	WEAK symbol<ScreenPlacement*()> ScrPlace_GetViewPlacement{0x362840};
	WEAK symbol<float()> ScrPlace_HiResGetScaleX{0x362910};
	WEAK symbol<float()> ScrPlace_HiResGetScaleY{0x362930};

	WEAK symbol<void(XAssetType type, void(__cdecl* func)(XAssetHeader, void*), const void* inData, bool includeOverride)>
	DB_EnumXAssets_Internal{0x394C60};
	WEAK symbol<const char*(const XAsset* asset)> DB_GetXAssetName{0x366140};
	WEAK symbol<int(XAssetType type)> DB_GetXAssetTypeSize{0x366180};
	WEAK symbol<XAssetHeader(XAssetType type, const char* name, 
		int createDefault)> DB_FindXAssetHeader{0x3950C0};
	WEAK symbol<void(void* levelLoad, const char* name, 
		const unsigned int allocFlags, const unsigned __int64 sizeEst)> DB_LevelLoadAddZone{0x396B50};

	WEAK symbol<int(XAssetType type, const char* name)> DB_IsXAssetDefault{0x3968C0};
	WEAK symbol<int(XAssetType type, const char* name)> DB_XAssetExists{0x39B7B0};

	WEAK symbol<int(const RawFile* rawfile)> DB_GetRawFileLen{0x3961B0};
	WEAK symbol<int(const RawFile* rawfile, char* buf, int size)> DB_GetRawBuffer{0x396080};
	WEAK symbol<char*(const char* filename, char* buf, int size)> DB_ReadRawFile{0x3994B0};

	WEAK symbol<bool(const char* zone, int source)> DB_FileExists{0x394DC0};
	WEAK symbol<void(XZoneInfo* zoneInfo, unsigned int zoneCount, DBSyncMode syncMode)> DB_LoadXAssets{0x397500};
	WEAK symbol<bool(const char* zoneName)> DB_IsLocalized{0x396790};

	WEAK symbol<void(int client_num, const char* menu, 
		int is_popup, int is_modal, unsigned int is_exclusive)> LUI_OpenMenu{0x1E1210};
	WEAK symbol<void(int clientNum, const char* menu, int immediate,
		hks::lua_State* state)> LUI_LeaveMenuByName{0x26BE80};
	WEAK symbol<void()> LUI_EnterCriticalSection{0x2669B0};
	WEAK symbol<void()> LUI_LeaveCriticalSection{0x26BDC0};

	WEAK symbol<bool(int clientNum, const char* menu)> Menu_IsMenuOpenAndVisible{0x1AAF10};
	WEAK symbol<void(int clientNum, const char* menu)> Menus_OpenByName{0x1E1270};
	WEAK symbol<void(int clientNum, const char* menu)> Menus_CloseByName{0x1DA4C0};
	WEAK symbol<void*(void* dc, const char* name)> Menus_FindByName{0x1AC810};
	WEAK symbol<void(void* dc, void* menu, int a3)> Menus_Open{0x1E1296};
	WEAK symbol<void(void* dc)> Display_MouseMove{0x180B70};

	WEAK symbol<scr_string_t(const char* str)> SL_FindString{0x507FD0};
	WEAK symbol<scr_string_t(const char* str, unsigned int user)> SL_GetString{0x5083A0};
	WEAK symbol<const char*(scr_string_t stringValue)> SL_ConvertToString{0x507CD0};
	WEAK symbol<unsigned int(const char* str)> SL_GetCanonicalString{0x504A00};

	WEAK symbol<void(netadr_s* from)> SV_DirectConnect{0x54DBF0};
	WEAK symbol<void(int arg, char* buffer, int bufferLength)> SV_Cmd_ArgvBuffer{0x1CAC60};
	WEAK symbol<void(const char* text_in)> SV_Cmd_TokenizeString{0x1CACE0};
	WEAK symbol<void()> SV_Cmd_EndTokenizedString{0x1CACA0};

	WEAK symbol<gentity_s*(const char* name)> SV_AddBot{0x54D4F0};
	WEAK symbol<bool(int clientNum)> SV_BotIsBot{0x53B6D0};
	WEAK symbol<const char*()> SV_BotGetRandomName{0x53ABD0};
	WEAK symbol<int(gentity_s* ent)> SV_SpawnTestClient{0x550580};

	WEAK symbol<const char*(int clientNum)> SV_GetGuid{0x551D90};
	WEAK symbol<int(int clientNum)> SV_GetClientPing{0x551D70};
	WEAK symbol<playerState_s* (int num)> SV_GetPlayerstateForClientNum{0x551E10};
	WEAK symbol<void(int index, const char* string)> SV_SetConfigstring{0x553E60};
	WEAK symbol<bool()> SV_Loaded{0x553970};
	WEAK symbol<void(int clientNum, const char* reason)> SV_KickClientNum{0x54C060};
	WEAK symbol<bool(const char* map)> SV_MapExists{0x54C0C0};
	WEAK symbol<void(int localClientNum)> SV_FastRestart{0x54BE00};
	WEAK symbol<void(void* cl, int type, const char* fmt, ...)> SV_SendServerCommand{0x1CC040};
	WEAK symbol<void(client_t* drop, const char* reason, bool tellThem)> SV_DropClient_Internal{0x54E7F0};

	WEAK symbol<void(const char* error, ...)> Sys_Error{0x1D8710};
	WEAK symbol<int()> Sys_Milliseconds{0x5BDDF0};
	WEAK symbol<bool()> Sys_IsDatabaseReady2{0x4F79C0};
	WEAK symbol<bool(int, void const*, const netadr_s*)> Sys_SendPacket{0x5BDA90};
	WEAK symbol<HANDLE(Sys_Folder, const char* baseFilename)> Sys_CreateFile{0x5B2860};

	WEAK symbol<const char*()> SEH_GetCurrentLanguageCode{0x585090};
	WEAK symbol<const char*()> SEH_GetCurrentLanguageName{0x5850F0};

	WEAK symbol<const char*(const char*)> UI_GetMapDisplayName{0x4DDEE0};
	WEAK symbol<const char*(const char*)> UI_GetGameTypeDisplayName{0x4DD8C0};
	WEAK symbol<void(unsigned int localClientNum, const char** args)> UI_RunMenuScript{0x1E35B0};
	WEAK symbol<int(const char* text, int maxChars, Font_s* font, float scale)> UI_TextWidth{0x1E6760 };
	WEAK symbol<void(void* dc, void* menuList, int close)> UI_AddMenuList{0x1D9960};

	WEAK symbol<const char*(const char* string)> UI_SafeTranslateString{0x4E8BC0};
	WEAK symbol<void(ScreenPlacement* scrPlace, const char* text, rectDef_s* rect, Font_s* font, float x, float y,
		float scale, const float* color, int style, int textAlignMode, rectDef_s* textRect, char a12)> UI_DrawWrappedText{0x1DCE30};

	WEAK symbol<int(int local_client_num, int menu)> UI_SetActiveMenu{0x1E4D80};

	WEAK symbol<int(const char*, game::StringTable**)> StringTable_GetAsset{ 0x5A0A80 };
	WEAK symbol<char*(game::StringTable*, int, int)> StringTable_GetColumnValueForRow{ 0x5A0AC0 };
	WEAK symbol<int(game::StringTable*)> StringTable_GetRowCount{ 0x5A0B00 };
	WEAK symbol<bool(const char*, const char*)> UI_ActivisionClanTagAllowedForGamerTag{ 0x1D9800 };

	WEAK symbol<void*(jmp_buf* Buf, int Value)> longjmp{0x826710};
	WEAK symbol<int(jmp_buf* Buf)> _setjmp{0x8A3190};

	WEAK symbol<game::cg_s*()> getCGArray{0x5CB80};

	WEAK symbol<void(int localClientNumber, const char* caller)> LiveStorage_EnsureMaySetPersistentData{ 0x19C2F0 };
	WEAK symbol<DDLState*(DDLState* result, const DDLDef* ddlDef)> DDL_GetRoot{ 0x7938A0 };
	WEAK symbol<bool(DDLState const* a1, DDLState* a2, int a3, unsigned int const* a4)> DDL_MoveToPathByHash{ 0x793DD0 };
	WEAK symbol<bool(DDLState* a1)> DDL_StateIsLeaf{ 0x7945B0 };
	WEAK symbol<bool(DDLState* state, DDLContext* buffer, const char* value)> Com_DDL_SetDataFromString{ 0x1592E0 };
	WEAK symbol<bool(int localClientNumber)> LiveStorage_StatsWriteNeeded{ 0x1592E0 };

	WEAK symbol<bool(const DDLState* state, DDLContext* ddlContext, int val)> DDL_SetInt{ 0x7941A0 };
	WEAK symbol<bool(const DDLState* state, DDLContext* ddlContext, unsigned __int64 val)> DDL_SetUInt64{ 0x794340 };
	WEAK symbol<bool(const DDLState* state, DDLContext* ddlContext, float val)> DDL_SetFloat{ 0x794140 };
	WEAK symbol<bool(const DDLState* state, DDLContext* ddlContext, const float val)> DDL_SetFixedPoint{ 0x7940A0 };
	WEAK symbol<bool(const DDLState* state, DDLContext* ddlContext, const char* val)> DDL_SetString{ 0x794270 };
	WEAK symbol<bool(const DDLState* state, DDLContext* ddlContext, const char* val)> DDL_SetEnum{ 0x794010 };

	/***************************************************************
	 * Variables
	 **************************************************************/

	WEAK symbol<CmdArgs> sv_cmd_args{0x2ED1EB0};

	WEAK symbol<int> g_script_error_level{0xB7AC1A4};
	WEAK symbol<jmp_buf> g_script_error{0xB7AC2C0};
	
	WEAK symbol<unsigned int> levelEntityId{0xB1100B0};
	WEAK symbol<unsigned int> gameEntityId{0xB1100B4};

	WEAK symbol<const char*> command_whitelist{0x10ACB70};
	WEAK symbol<cmd_function_s*> cmd_functions{0x344DF18};
	WEAK symbol<CmdArgs> cmd_args{0x2ED1E00};
	WEAK symbol<CmdArgsPrivate> cmd_argsPrivate{0x3513F20};

	WEAK symbol<int> connectionState{0x2EC82C8};

	WEAK symbol<int> g_poolSize{0x10B3C80};
	WEAK symbol<int> g_compressor{0x3962804};

	WEAK symbol<scrVarGlob_t> scr_VarGlob{0xB138180};
	WEAK symbol<scrVmPub_t> scr_VmPub{0xB7AE3C0};
	WEAK symbol<function_stack_t> scr_function_stack{0xB7B8940};

	WEAK game::symbol<unsigned __int64> pmem_size{0xC92E1D8};
	WEAK game::symbol<unsigned char*> pmem_buffer{0xC92E1D0};

	WEAK game::symbol<PhysicalMemory> g_mem{0xC92E1E0};
	WEAK game::symbol<PhysicalMemory> g_scriptmem{0xC92EC40};
	WEAK game::symbol<PhysicalMemory> g_physmem{0xC92F6A0};

	WEAK game::symbol<unsigned __int64> stream_size{0x258AA10};
	WEAK game::symbol<unsigned char*> stream_buffer{0x258AA08};

	WEAK symbol<GfxDrawMethod_s> gfxDrawMethod{0xE9213F0};

	WEAK symbol<int> dvarCount{0x2999C34};
	WEAK symbol<dvar_t> dvarPool{0x344DF20};

	WEAK symbol<void*> g_assetPool{0x10B4460};
	WEAK symbol<const char*> g_assetNames{0x10B30D0};
	WEAK symbol<XZoneInfoInternal> g_zoneInfo{0x5F5A370};
	WEAK symbol<unsigned short> g_zoneIndex{0x3D1008C};

	WEAK symbol<DB_FileSysInterface*> db_fs{0x1566C08};

	WEAK symbol<int> keyCatchers{0x2EC82C4};
	WEAK symbol<PlayerKeyState> playerKeys{0x2999E1C};

	WEAK symbol<SOCKET> query_socket{0xC9DCD38};

	WEAK symbol<DWORD> threadIds{0xAC80740};

	WEAK symbol<int> ui_num_arenas{0xAA55AD8};
	WEAK symbol<int> ui_arena_buf_pos{0xAA55ADC};
	WEAK symbol<char*> ui_arena_infos{0xAA55AE0};
	WEAK symbol<ui_info> ui_info_array{0x352E580};

	WEAK symbol<int> level_time{0x7361F9C};
	WEAK symbol<int> com_frameTime{0x2E6EE34};

	WEAK symbol<map_t> maps{0x926C80};

	WEAK symbol<ID3D11Device*> d3d11_device{0x12DFBF8};

	WEAK symbol<HWND> hWnd{ 0xC9DD2E0 };

	WEAK symbol<__int64> controllerStatData{ 0x299CF50 };

	WEAK symbol<gentity_s> g_entities{0x71F19E0};
	WEAK symbol<client_t*> svs_clients{0x2DC3390};
	WEAK symbol<int> svs_numclients{0x2DC338C};
	WEAK symbol<int> gameTime{0x7361F9C};
	WEAK symbol<int> maxClients{ 0x7361F90 };
	WEAK symbol<int> num_entities{ 0x7361A40 };

	WEAK symbol<int> svs_time{0x2DC3380};

	WEAK symbol<clientConnection_t*> clientConnections{0x2EC8510};

	WEAK symbol<int> sv_serverId_value{0xB7F9630};

	WEAK symbol<bool> virtualLobby_loaded{0x2E6EC9D};

	WEAK symbol<client_state_t*> client_state{0x2EC84F0};
	WEAK symbol<connect_state_t*> connect_state{0x2EC8510};

	WEAK symbol<XZone> g_zones{0x5F292B0};

	WEAK symbol<Material*> h1_hud_weapwidget_ammopip_shell { 0x3909C98 };

	WEAK symbol<playerState_s*> playerState{ 0x2DE3600 };

	WEAK symbol<refdef_t*> refdef { 0x390B580 };
	WEAK symbol<PathData> pathdata { 0x71AF210 };

	WEAK symbol<WeaponDef*> bg_weaponDefs{ 0x389EB40 };

	WEAK symbol<voiceCommunication_t> cl_voiceCommunication{0x2FCC7A0};

	WEAK symbol<int[18]> qword_C9DD1B0{0xC9DD1B0};
	WEAK symbol<int[18]> qword_2E6FA3C{0x2E6FA3C};
	WEAK symbol<infoParm_t> infoParms{ 0x12DB540 };

	WEAK symbol<char[8]> clanName{0x3920896};

	WEAK symbol<void*> sessionData{0x3426D20};

	WEAK symbol<SessionData> g_serverSession{ 0xB807260 };

	WEAK symbol<int[18]> s_clientTalkTime{ 0xC9DD1B0 };

	WEAK game::symbol<int> cl_maxLocalClients{ 0x2E6EE30 };

	namespace hks
	{
		WEAK symbol<lua_State*> lua_state{0x36647F8};
		WEAK symbol<void(lua_State* s, const char* str, unsigned int l)> hksi_lua_pushlstring{0x79800};
		WEAK symbol<HksObject*(HksObject* result, lua_State* s, const HksObject* table, const HksObject* key)> hks_obj_getfield{0x22CB90};
		WEAK symbol<void(lua_State* s, const HksObject* tbl, const HksObject* key, const HksObject* val)> hks_obj_settable{0x22DDB0};
		WEAK symbol<HksObject* (HksObject* result, lua_State* s, const HksObject* table, const HksObject* key)> hks_obj_gettable{0x22D070};
		WEAK symbol<void(lua_State* s, int nargs, int nresults, const unsigned int* pc)> vm_call_internal{0x25D2F0};
		WEAK symbol<HashTable*(lua_State* s, unsigned int arraySize, unsigned int hashSize)> Hashtable_Create{0x21B030};
		WEAK symbol<cclosure*(lua_State* s, lua_function function, int num_upvalues, 
			int internal_, int profilerTreatClosureAsFunc)> cclosure_Create{0x21B250};
		WEAK symbol<int(lua_State* s, int t)> hksi_luaL_ref{0x236CC0};
		WEAK symbol<void(lua_State* s, int t, int ref)> hksi_luaL_unref{0x22FC40};
		WEAK symbol<int(lua_State* s, const HksCompilerSettings* options, const char* buff, 
			unsigned __int64 sz, const char* name)> hksi_hksL_loadbuffer{0x22E620};
		WEAK symbol<int(lua_State* s, const char* what, lua_Debug* ar)> hksi_lua_getinfo{0x22FFE0};
		WEAK symbol<int(lua_State* s, int level, lua_Debug* ar)> hksi_lua_getstack{0x2302B0};
		WEAK symbol<void(lua_State* s, const char* fmt, ...)> hksi_luaL_error{0x22F930};
		WEAK symbol<void(lua_State* s, int what, int data)> hksi_lua_gc{0x236EF0};
		WEAK symbol<const char*> s_compilerTypeName{0x10AD750};
	}
}

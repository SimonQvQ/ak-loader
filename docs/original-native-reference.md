# Original native module reference (static evidence)

Target: original module member `lib/arm64-v8a/libxmg_mrfz.so` (local analysis input; not distributed).
SHA256: `702932485a4a05aff2a4310bc049eb950c38ad963f6a4865ea31a9d226d3251e`
Tools: NDK 27.1 llvm-nm -D -C, llvm-objdump -d -C, readelf -Wr; bounded Python byte decoding. These are direct LLVM/ELF observations, not REA retained Evidence records. REA native engine unavailable (Ghidra not configured). No device operations; no target execution. Addresses are ELF virtual addresses, before ASLR.

## Names recovered from actual selector bytes

Selectors use a flag byte followed by ciphertext; byte i is xor'ed with `(i + key) & 255`. Repeated opaque xor3 operations cancel on the initial global-state path. These results are verified against materialized data and their BNM consumers, not inferred from function names.

| Selector | Ciphertext/consumer evidence |
|---|---|
| `Assembly-CSharp` (no `.dll`) | symbol0xe908, key0xa3, image accessor0x38480 |
| `Torappu.Battle.BattleController` | symbol0xe919, key0xa5, clazz accessor0x37b1c |
| `Torappu.Battle.Enemy` | symbol0xe96d, key0xb2, clazz accessor0x392dc |
| `Torappu.Battle.Entity` | symbol0xe983, key0xb6, clazz accessor0x39c44 |
| `get_instance` | inline stack materialization0x37350..3737c, key0xa7, accessor0x371c8 |
| `_scheduler` | inline stack materialization0x314ec..31510, key0xbb, GetField0x31c5c |
| `<totalEnemiesCnt>k__BackingField` | symbol0xe93a, key0xbc, GetField0x2ed14 |
| `m_finishedEnemiesCnt` | symbol0xe8aa, key0xbd, GOT0x573b0 |
| `m_killedEnemiesCnt` | symbol0xe8c0, key0xbe, GOT0x573b8 |
| `m_validFinishedEnemiesCnt` | symbol0xe8d4, key0xbf, GOT0x573c0 |
| `m_validKilledEnemiesCnt` | symbol0xe8ef, key0xc0, GOT0x573c8 |
| `_sideTypeIndex` | selector0xe820, key0xca, GetField0x2b670 |
| `OnHpZero` | inline stack materialization0x2b82c..2b84c, key0xcc; invoked with2 args |
| `FinishGame` | inline stack materialization0x2c2e4..2c308, key0xcf |
| `Immune` | inline stack materialization0x2a04c..2a078, key0xc4; used by regexSearchString |
| `_SetNumCharacterLimit` | selector0xe893, key0xd2 |
| `get_playerSide` | selector0xe830, key0xd4 |
| `baseAttackTime` | selector0xe800, key0x95 |
| `magicResistance` | symbol0xe840, key0x83 |

Last four selectors are decoded names; this report does not claim full current-game method or field layouts. The attributes initializer contains more inline constants; the above is not a complete attribute-key inventory.

## Controller/scheduler object flow

`GameTool::getBattleController()` at0x37054 obtains the0-parameter `get_instance` BNM method (call0x37138), calls it, then constructs BNM::Class from the returned object (0x37198). It returns `(class, instance)` in x0,x1 (0x3719c/0x371b0). Missing method/instance yields a null instance. It does not retain an instance captured by StartGame.

`GameTool::getScheduler()` at0x3136c calls getBattleController at0x31454. It resolves `_scheduler` on that class (GetField0x31c5c), binds the controller instance (0x31c6c), then reads an8-byte value from the field (0x31cbc), or uses BNM GetStaticValue for corresponding field flags. It constructs Class(schedulerObject) at0x31cd0 and returns `(schedulerClass,schedulerObject)`. This establishes a managed-object reference-sized field. The scheduler's actual managed full class name is not encoded by this helper and remains unknown.

`hook_FinishGame` at0x2e4b4 calls getScheduler at0x2e4e8, resolves `<totalEnemiesCnt>k__BackingField` (0x2ed14), binds the returned scheduler instance (0x2ed24), and obtains a32-bit value using GetFieldPointer+ldr w0 (0x2ed68/0x2ed70), or GetStaticValue. It calls setKilledEnemies(total) at0x2ed74, then the original FinishGame at0x2ed94 with all three explicit arguments preserved. No MenuData enable flag is read anywhere in this hook; the counter rewrite is unconditional on the ordinary opaque-predicate path.

`GameTool::setKilledEnemies(int)` at0x2edc4 constructs a string list in this order and applies the SAME input int:

1. getScheduler0x310c4, `m_finishedEnemiesCnt`, setObscuredTypes0x310f8.
2. getScheduler0x310fc, `m_killedEnemiesCnt`, setObscuredTypes0x3112c.
3. getScheduler0x31130, `m_validFinishedEnemiesCnt`, setObscuredTypes0x31160.
4. getScheduler0x31164, `m_validKilledEnemiesCnt`, setObscuredTypes0x31194.

All four fields are on the scheduler object, not the BattleController. The helper reacquires the scheduler for each write. The loop surrounding these writes computes its ordinary bound from opaque globals; it is not evidence of gameplay retries.

The counter helper does NOT write four plain native int fields. `setObscuredTypes`0x2ad10 resolves the field, obtains its type0x2ad80, calls `CreateNewObjectParameters<int>(value)`0x2ad90, then copies16 bytes from the returned pointer (0x2ad9c) into the field (0x2adc4), with static-field handling when applicable. The constructor helper0x36f54 resolves `.ctor` with1 arg and allocates a BNM instance before invocation. `CreateNewInstance`0x4637c returns the original object pointer; no explicit unbox/+header adjustment occurs at the16-byte copy. This is the original binary's behavior, NOT a recommendation to duplicate it. The exact field type and correct unboxed payload must be verified from current game metadata before implementation. The binary alone does not establish that the original counter-write strategy is semantically correct on the current game.

## Hook signatures and registration

The actual gameHook0x32b34 registers exactly these7 targets using BNM GetMethod(name,argCount) and DobbyHook. It does not use full managed type selectors:

| Method | count | return template | explicit C++ native hook arguments including this | GetMethod site |
|---|---:|---|---|---|
| get_gameResult |0|int|Object*|0x33718|
| ModifyLifePoint |4|bool|Object*,int,Object*,int,bool|0x34080|
| SetCost |2|int|Object*,int,int|0x349c8|
| StartGame |0|void|Object*|0x35318|
| FinishGame |3|void|Object*,int,bool,Object*|0x35c68|
| Enemy.MoveByRoute |2|Vector2|Object*,float,bool*|0x365bc|
| Entity.Init |7|void|Object*,Object*,int,int,int,Vector2,float,Object*|0x36f00|

C++ symbols and return templates do not recover the actual managed enum/reference class names. Hidden MethodInfo passing is selectively added by BNM's invocation wrappers; exported hook signatures themselves omit it.

No separate Damage/Hit/OnDamage target is registered by gameHook. Its7 DobbyHook sites0x32ca0,0x32cd8,0x32d10,0x32d48,0x32d80,0x32db8,0x32df0 account for all its registrations. unityHook0x4297c registers set_timeScale at0x429cc. Other DobbyHook sites in the full ELF belong to BNM setup/loading/class runtime support. Exported hook-symbol inventory has these7 game hooks plus hook_set_timeScale; no exported damage/Hit/OnDamage hook was found. This bounded static result does not exclude arbitrary runtime instrumentation in the separate libxmg_core.so.

## Feature behavior / ordering

- StartGame hook0x2c110 calls original first0x2c14c. If MenuData::autoFinishGame (GOT0x57310 ->0x58fa0) is enabled, it resolves FinishGame with3 args and invokes `(1,false,null)` at0x2ca9c. These values are observed, but the current game's managed enum meaning is not inferred.
- get_gameResult hook0x328bc checks MenuData::quitAutoFinishGame (GOT0x57328 ->0x58fd0). Enabled: returns1 directly0x32b28, no call to original. Disabled: tail-calls original0x32b14..0x32b24. It does not call original first or inspect its result.
- ModifyLifePoint0x326ec uses invincible flag (GOT0x57360 ->0x59060); enabled branch sets first integer argument to0 at0x327e4, then calls original and propagates its bool. It does not short-circuit returntrue.
- SetCost0x31d08 uses maxCost flag (GOT0x57350 ->0x59030); enabled replaces first int by999. Initial encrypted817 at0x31eac xor0xd6 at0x323a0 yields999. The second int is unchanged.
- Enemy.MoveByRoute hook0x2adfc is autoKillMonsters, not freeze. If flag(GOT0x57300 ->0x58f70) is on and `_sideTypeIndex==1`, resolves OnHpZero with2 args and invokes `(true,true)`; then still calls original MoveByRoute0x2c0d0. Its result is propagated as Vector2.
- Entity.Init0x29b3c checks superAttribute(GOT0x572c8 ->0x58f20) and first explicit integer enum equal1 OR4. It changes configured fields on the Object* passed as x1, before original Init. It additionally enumerates fields and regex-matches `Immune` (two boolean options1,0 supplied at0x2a7e4/0x2a7e8; option meanings not resolved here), writes booltrue for matches, then calls original. This is attribute editing, not evidence of a final-damage multiplier hook.

## BNM lifecycle

InitBNM::init0x41408 checks imported InitUnity::il2cppElf state, then:
- SetMethodFinder(KittyMemoryFinder, &InitUnity::il2cppElf)0x41480;
- AllowLateInitHook0x41484;
- AddOnLoadedEvent(empty0x415c0)0x41490;
- AddOnLoadedEvent(unityHook)0x4149c, GOT0x574c8;
- AddOnLoadedEvent(gameHook)0x414a8, GOT0x57400;
- TryLoadByUsersFinder0x414b4.

This reference establishes loading readiness callbacks and StartGame/FinishGame lifecycle hooks. It does not show a general-purpose main-thread queue or get_gameResult polling thread in this module. Using a queue executed from a verified game-thread callback is an implementation decision, not recovered original behavior.

Full assembly: `/tmp/xmg-full.asm`; focused `/tmp/xmg-hooks.asm`, `/tmp/xmg-accessors.asm`, `/tmp/xmg-instance.asm`, `/tmp/xmg-attributes.asm`. No current-game metadata claims are made by this report.

## Follow-up: BNM layout and setup comparison (2026-10-22)

Compared original module's actual AArch64 loads with current cached BNM selected UNITY_VER213, UNITY_PATCH_VER39. Current offsets were obtained by compiling a temporary offsetof-only translation unit with NDK27.1 aarch64 compiler; no repository modification or device execution. Source/output: `/tmp/zoot-bnm-offsets.cpp`, `/tmp/zoot-bnm-offsets.s`.

| Field | Original instruction | Current compiled offset | Match |
|---|---|---:|---|
| Il2CppThread.internal_thread |0x48558 `ldr x8,[thread,#0x10]`|0x10|yes|
| Il2CppDomain.default_context |0x48560 `ldr x9,[domain,#0x10]`|0x10|yes|
| Il2CppInternalThread.current_appcontext |0x48564 `ldr x8,[internal,#0x70]`|0x70|yes|
| Il2CppClass.parent |0x49920|0x58|yes|
| Il2CppClass.methods |0x49934|0x98|yes|
| Il2CppClass.method_count |0x49928/0x4995c|0x11c|yes|
| Il2CppClass.instance_size |0x463c4|0xf8|yes|
| MethodInfo.name |0x49950|0x18|yes|
| MethodInfo.parameters_count |0x36fd0 and many BNM invocation wrappers|0x52|yes|

These offsets do not uniquely identify the exact Unity patch used to compile the original because several Unity versions share layouts. They do refute the specific hypothesis that the original late-init succeeds by reading different domain/thread offsets. They do NOT establish the current game's actual memory layout.

Original BNM_il2cpp_init0x4845c and BNM_Class$$FromIl2CppType0x484d8 match upstream control flow: init path unhooks late-init first when enabled, calls original init, unhooks init, calls Load; late path calls original FromType first, checks state, lazily resolves domain_get/thread_current, calls both, checks non-null thread/internal and compares the above contexts, unhooks init and late target, then calls Load synchronously. Domain itself is not checked for null in the original or current upstream hook before reading default_context.

Original Load0x49400 calls SetupBNM0x49424, LoadDefaults0x49428, SetupCoroutine/ProcessCustomClasses/LoadCoroutine0x4942c..49434, then sets states.state=true0x49458, then invokes loaded events. No explicit loading-state/once guard is visible before SetupBNM. Current source's optional multi-thread synchronization is disabled in configuration; even its optional `shared_lock` is not a one-shot initialization mutex. Concurrent late/init entry and early null domain are possibilities to instrument, not established causes of the reported Houdini crash.

Current configuration disables ClassesManagement/Coroutine, unlike original's compiled code, reducing its setup hooks. Original SetupBNM0x49544 and upstream source share the first-B/BL discovery chains and method-table layout. Original branch helper0x4a92c is unbounded and returns the first unconditional B/BL target. Both original and current decoder retain only24 displacement bits before shifting2; that implementation is unsafe for some distant AArch64 branch displacements, but no mismatch occurs in the specific inspected current-game export chains below.

Offline current-game ELF `/tmp/zoot-game-libil2cpp.so` byte-derived paths (not runtime mappings):
- array_new_specific export0x239960c contains B→0x23cde78; firstBL there at0x23cde8c targets0x23f8964, which BNM stores as Class::Init. Target0x23f8970 reads Class flag byte at+0x136. Both BNM's decoder and correct26-bit ARM decoder agree on these targets.
- class_from_il2cpp_type export0x2399650, B at0x2399654→0x23f8434, the late-init hook target. Both decoders agree.
- domain_get_assemblies export0x2399c60, firstBL at0x2399c68→0x23ce070, Assembly::GetAllAssemblies candidate. Both decoders agree.

Loader threading: original mrfz initializer0x15024 calls Core::initCore0x15040, then Tool::Thread::runTaskAtIntervalEx(callback,1000)0x1506c. Callback sdgkjfdal0x14ff0 checks Verify::isVerify and invokes InitBNM when it becomes true. Original Core::initCore in separate libxmg_core.so0x846980 dispatches runTaskAfterDelay(...,1), and InitUnity::init0x80243c polls library mappings at1-second intervals. Original KittyMemoryFinder0x40df8 also polls libil2cpp mapping readiness at1-second intervals; it does not attach a managed thread. Crucially, BNM actual setup runs synchronously on whichever managed thread reaches its init/late-init hook, not on the polling thread. No special MuMu/Houdini loader-thread switch was observed in these functions.

Next evidence to collect if needed: phase logs for late-hook entry/original-return/domain/thread/internal/context compare/unhook/Load/SetupBNM; log only first entries to avoid hot-path flooding. Confirm whether Load was entered once, and which thread reaches it. A passed ADRP self-test does not by itself verify patch/unpatch while another translated thread executes the target. No crash cause is claimed from the available static evidence.

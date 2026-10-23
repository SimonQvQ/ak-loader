# Current game native evidence (2026-10-22)

Target: installed Arknights 2.7.81 ARM64 `libil2cpp.so`.
Local cache: `build/cache/libil2cpp.so`; size216663520 bytes;
SHA256 `53c12224664f6fd4265905d12f5aaf74e315bc76584a5399d68c68defee14d2b`.

This report records ordinary LLVM/ELF and bounded read-only process-memory observations. REA native tools were unavailable in this session; these are not REA retained Evidence IDs. The cache was pulled from the installed app's `lib/arm64/libil2cpp.so`, without copying the APK. The ELF has out-of-range symbol section indices, which make LLVM reject whole-file disassembly. Selected original bytes were wrapped in a minimal ELF section for decoding; the code bytes were not modified. ELF VAs below precede ASLR.

## Current class vtable layout

Plaintext disk code for Entity.OnTakeDamage, method identity established by `build/x64-test-logs/expanded-metadata.txt`, contains this interface-dispatch sequence:

- `0x267fbac`: add the computed slot index shifted by4 to the class pointer.
- `0x267fbb0`: `add x0, x8, #0x140`.
- `0x267fbb4`: `ldp x8, x3, [x0]`, then invokes x8 with x3 as hidden MethodInfo.

Therefore the current game's `VirtualInvokeData` array begins at class+0x140, with code pointer at+0 and MethodInfo at+8, stride16. This is independent confirmation of the required padding beyond the known class flag bytes at0x136..0x137. A BNM compiler layout whose vtable begins0x138 reads a code address as a MethodInfo pointer when it loads slot0's method at0x140.

The same base0x140 is present in live BattleController.StartGame dispatch at0x259d94c and FinishGame dispatch at0x259dc9c. These are corroborating current-process bytes, not an inference from Unity version alone.

## BattleController lifecycle and one-click finish

Disk bytes for this controller region are protected; decoding disk bytes does not establish valid native control flow. The observations below use initialized game process3515, guest library base0x400023800000, read with bounded `/proc/3515/mem` ranges, with no writes or game actions. The first12 bytes of StartGame/FinishGame are already patched by the current module; conclusions use their surviving original bodies and separate unpatched helpers.

Runtime metadata establishes:

- StartGame: instance void(), ELF0x259d6c0.
- FinishGame: instance void(GameResult,bool,System.Action), ELF0x259d970.
- _DoFinishGame: instance void(GameResult,bool), ELF0x259dccc.
- GameResult.NOT_YET=0, WIN=1, LOSE=2.

Observed FinishGame guard:

1. At0x259da18 it calls0x2596844; false branches directly to the epilogue at0x259dadc, with no finish processing.
2. The helper's ordinary path0x25968ac calls0x25968c8, compares the returned value to2, and returns equality.
3. The underlying getter0x2596930 reads controller+0x220.

Thus ordinary FinishGame only proceeds when controller state equals2. The accessor and guard can be overridden by the game's XLua hotfix delegates; those branches exist in the code.

Observed StartGame flow:

- It constructs an Action bound to `<StartGame>b__392_0` and passes it into the selected GameMode start implementation/interface dispatch. Its own ordinary body does not directly set state2.
- The callback's tail0x25b97c4 loads2 into w1 and at0x25b97cc invokes0x2597b40 on the controller, followed by a status notification.
- The corresponding finish tail0x259e128 loads3 and invokes the same setter at0x259e130, confirming a state transition shared by start and finish.

Inference: calling FinishGame immediately after StartGame returns can be too early if the GameMode start callback runs later. The observed code establishes the guard and callback ordering; it does not alone prove that a particular selected GameMode invokes the callback asynchronously. A finish action after the verified callback's original return, or on another verified game-thread callback once state2 is observed, avoids this unresolved timing dependency.

Observed finish boolean behavior:

- FinishGame preserves its bool through the GameMode finish delegate into _DoFinishGame.
- _DoFinishGame notifies the GameMode and registered systems, writes the result to controller+0x224, sets state3, then checks this bool at0x259e17c.
- false invokes an additional controller result-processing method0x259e5ac(result); true skips that call.
- Both paths then invoke the stored `m_onGameDoFinish` Action at0x259e18c..0x259e194.

The original module passes false. That preserves this additional ordinary processing path. The recovered metadata does not supply the bool's source-level parameter name, so calling it `immediate` remains a local implementation label, not a proven managed name.

## Scheduler counter boundary

Expanded current metadata establishes the four relevant fields as ObscuredInt at object offsets44,64,84,104 (20-byte spacing), and totalEnemiesCnt as ordinary Int32 at296. The original module's16-byte copy is not correct evidence for the current value size. Calling the current ObscuredInt conversion operator through runtime_invoke, unboxing its result and setting the field through the current runtime is a source-bound alternative to guessing its payload.

Synchronizing those four counters reproduces the original module's stated counter selection. It does not prove all stage-specific win conditions, mission objectives, life-loss/star conditions, server acceptance, or event rewards. Those require runtime practice verification and the actual stage/mode; no such outcome was observed by this read-only investigation.

## Damage investigation state

Current metadata distinguishes Modifier.get_isDamage/get_isHeal and carries source/target ObjectPtr values, m_value/realDelta FP values. Entity.OnTakeDamage performs cancellation/ability/event notification calls; no direct HP assignment is present in the inspected body. It should not be assumed to be the final HP-application point solely from its name. Investigation was paused to prioritize the requested one-click finish; no final-damage hook has been established by this report.

## Damage follow-up: verified delta application (2026-10-22)

Additional source: `build/x64-test-logs/basic-features-start.txt` supplies current FP and PlayerSide definitions across assemblies. Read-only process4350 supplied bounded initialized code ranges. Complete Entity method names were read from its current class method table (class pointer reached through the same GOT/TypeInfo chain used by Entity code); this avoids assigning names from guesses. Method table result retained locally in `/tmp/zoot-entity-methods.txt`, selected page snapshots in `/tmp/zoot-readonly-pages-4350/`.

### Relevant methods and machine ABI

| Current metadata/name evidence | ELF VA | Native shape established |
|---|---:|---|
| Entity.ApplyModifier (method table) |0x267ba1c|live body inspected in process6501; disk body protected|
| Entity._OnBeforeApplyingModifier (method table) |0x267be2c|runs before _OnApplyingModifier/DoApplyModifier|
| Entity._OnApplyingModifier (method table) |0x267c0ac|runs OnTakeDamage before DoApplyModifier|
| Entity.DoApplyModifier (method table) |0x267d360|bool(Object* this, Modifier* ref, bool, MethodInfo*) candidate; see qualification below|
| Entity._DoApplyEPModifierInternal (method table) |0x267da50|elemental-damage helper; not the ordinary HP branch|
| Modifier.get_finalDelta (runtime metadata) |0x283ddb4|FP(Modifier* unboxedThis, MethodInfo*)|
| Modifier.get_isDamage |0x283d148|bool(Modifier* unboxedThis, MethodInfo*)|
| Modifier.get_source |0x283d51c|Entity*(Modifier* unboxedThis, MethodInfo*)|
| Modifier.get_target |0x283d5d0|Entity*(Modifier* unboxedThis, MethodInfo*)|
| Entity.get_side |0x25d8584|SideType(Object* this, MethodInfo*)|
| Entity.set_hp |0x266d564|void(Object* this, FP, MethodInfo*)|

DoApplyModifier's observed MethodInfo was0x7f4417abb5f8. It records parameters_count=2, flags0x1c4 and slot0x7c. Its return type is0x4000303d75e8; the type record's type byte is0x02 (Boolean), also present in the corresponding disk bytes. The body preserves x0 as entity, x1 as a mutable Modifier address and w2 as boolean; it writes back realDelta through that address, rather than making a by-value copy. This strongly establishes the machine ABI above. The parameter-type pointer table was not retained before the process exited, so the full managed first-parameter type (`Modifier&`) still needs one exact runtime type comparison before hook installation; source-level parameter names remain unknown. Preserve the hidden MethodInfo argument when calling the original.

FP is a value type in `Torappu.Common.dll`, namespace Torappu, with one Int64 `_serializedValue` at boxed offset16, hence unboxed offset0. Metadata constants: FRACTIONAL_PLACES=32, ONE=4294967296. Both implicit Int64 and Int32 conversions use `lsl x0,x0,#32` (ELF0x9510e6c,0x951134c); explicit conversion uses `asr x0,x0,#32` (0x9511344). A standalone FP passes and returns its8-byte payload in x registers, not floating-point registers. Integer multipliers can therefore multiply the signed raw Q32.32 payload directly, using a wider intermediate and checked saturation. Do not interpret it as a C++ float or double. Do not overflow signed64-bit arithmetic; retain sentinel/nonfinite cases unchanged unless their semantics are explicitly handled.

### Modifier object representation and getters

Current metadata gives these boxed offsets; native instance methods on this value type take the unboxed payload, so subtract16:

| Field | Boxed offset | Unboxed offset |
|---|---:|---:|
| m_source ObjectPtr<Entity> |16|0|
| m_target ObjectPtr<Entity> |32|16|
| m_value FP |48|32|
| realDelta FP |72|56|
| targetType |96|80|
| deltaType |100|84|
| damageType |104|88|
| originAtk FP |128|112|
| originValue FP |136|120|

Code confirms this representation independently: Modifier.get_value loads x0 from this+0x20; get_damageType loads w0 from this+0x58; get_originValue loads x0 from this+0x78. get_source passes the unchanged unboxed this address to the ObjectPtr<Entity> getter, while get_target passes this+0x10. Each ObjectPtr occupies16 bytes; neither should be reinterpreted as a direct Entity* reference. Call the current source/target getters so their object lifetime checks and optional hotfix behavior are preserved.

The ordinary get_isDamage path checks damageType is neither0 nor4, then targetType==0 (HP). It is separate from get_isHeal. Metadata establishes SideType.ALLY=1, ENEMY=2, NEUTRAL=4. Strict friendly-to-enemy filtering can use non-null getter results with source.get_side()==1 and target.get_side()==2, plus get_isDamage and a negative finalDelta. A neutral/environmental source, missing source, positive healing delta, friendly target or enemy source should retain the original value.

### Actual application chain and limits

DoApplyModifier's ordinary HP branch is source-bound evidence of delta application, unlike OnTakeDamage notification:

1.0x267d498..0x267d4a0 calls Modifier.get_finalDelta on the unboxed Modifier address and retains its FP result.
2. It applies further HP/minus-HP/status rules and reads the getter again as needed (e.g.0x267d684,0x267d6d8).
3. At0x267d81c it obtains current HP;0x267d83c..0x267d848 adds the retained delta through FP.op_Addition.
4.0x267d84c..0x267d854 calls Entity.set_hp with that sum.
5. It computes actual realDelta after property clamps/rules and writes Modifier+0x38 (unboxed realDelta) at0x267d9ac.

The scalar get_finalDelta getter derives the signed delta from deltaType and current m_value (including an optional hotfix branch). It does not itself subtract HP or write m_value. Multiple reads of the getter with a scaled return do not compound the stored m_value: each starts from the original current value. Within the inspected ordinary DoApplyModifier body, writes through the Modifier address affect realDelta rather than m_value.

A bounded implementation choice is a thread-local scope installed around the original DoApplyModifier, plus a get_finalDelta hook that scales only when its this pointer exactly equals the scoped Modifier. Save and restore the previous scope using RAII for nested modifiers. Query current get_isDamage, source, target and sides; call the original getter exactly once, require negative finite raw FP and multiply its result without writing m_value or attack attributes. Validating the Modifier target against the scoped entity would further reject mismatched contexts. This is a proposed implementation based on observed code, not a runtime-tested outcome.

This point is after construction of the calculated damage Modifier and any earlier processing that has already modified its current value. The upstream event/cancellation order was subsequently recovered from process6501, as detailed below. DoApplyModifier itself still performs additional HP/status/absorbing/clamping rules. Therefore do not claim this is strictly after every shield/mitigation rule, or that it bypasses those rules. The resulting multiplier remains subject to the game's original application path.

An observed override at ELF0x2a41268 reads get_finalDelta, takes a special fixed-one-HP path, sets realDelta to minus-one and returns without calling base DoApplyModifier; its alternate path tail-calls base at0x2a41414. A scope attached only to base DoApplyModifier will intentionally not cover that fixed-one path. Other overrides, XLua hotfix replacements, direct SetHpInternal calls and stage-specific mechanics may also bypass this selected scope/getter pair; coverage is not proven exhaustive. Negative direct-branch searches in the protected on-disk code are not evidence that no caller exists.

Remaining checks: verify DoApplyModifier exact managed parameter types in the current runtime; run practice with observed source/target sides and HP before/after; separately confirm friendly healing, incoming enemy damage and special fixed-hit/shield mechanics remain unchanged.

### Upstream processing order (process6501 read-only follow-up)

The initialized live ApplyModifier/_OnApplyingModifier bytes were sampled in process6501 without triggering game actions; `/tmp/zoot-applying-chain-live.asm` retains the selected decoding. The TypeInfo globals for Entity/Modifier still held lazy encoded handles at that instant, so those globals could not be used to read the method parameter table. This is a remaining type-verification gap, not proof that the types are absent.

Observed ApplyModifier ordinary flow:

1.0x267bb20 reads Modifier.get_isCancelled, aborting if true.
2. Unless SKIP_MODIFIER_EVENT is set,0x267bb7c invokes _OnBeforeApplyingModifier;0x267bb98 checks cancellation again.
3.0x267bbf4 invokes source.OnOutputModifier when source is valid.
4.0x267bc04 invokes target._OnApplyingModifier;0x267bc20 checks cancellation again.
5.0x267bc58 invokes source.OnBeforeTargetApplyModifier when valid.
6. A GameMode gate runs at0x267bcc8 or corresponding selected-mode targets.
7.0x267bcf0/0x267bcf4 load methodPointer/MethodInfo from entity class+0x900/+0x908 and0x267bcf8 invokes the pair. These offsets equal vtable0x140+slot0x7c*16, matching the current DoApplyModifier MethodInfo slot.
8. False DoApplyModifier returns abort; true subsequently calls OnAppliedModifier at0x267bd2c unless events were skipped.

Within _OnApplyingModifier,0x267c220 checks get_isDamage; true invokes the class vtable pair+0x970/+0x978 at0x267c250..0x267c258. The saved OnTakeDamage MethodInfo has slot0x83, and0x140+0x83*16=0x970, binding that dispatch to OnTakeDamage independently of names. Healing uses its separate subsequent branch.

Therefore a scope around base DoApplyModifier runs after the target's OnTakeDamage processing, front-end cancellation checks, source callbacks and the selected GameMode gate. This is substantially later than modifying attack stats or hooking the beginning of OnTakeDamage. It still precedes base DoApplyModifier's own final HP/status/clamp logic. Special overrides and hotfixes retain the coverage limits described above.

### FP finite boundaries (process6501)

The current FP class was reached through its own TypeInfo global (used by decoded FP methods), class0x7f43c80b6800, static_fields0x7f41ee025f00. Read-only static field reads with the metadata offsets establish:

- MaxValue at+16:9223372036854775806 (INT64_MAX-1).
- MinValue at+24:-9223372036854775806.
- PositiveInfinity at+64:9223372036854775807.
- NegativeInfinity at+72:-9223372036854775807.
- NaN at+80:-9223372036854775808 (INT64_MIN).

The original FP.IsInfinity body compares its scalar argument to the two static values at+0x48/+0x40; IsNaN compares to+0x50. This independently binds these values to the game's special-number semantics. A negative-damage multiplier should leave raw values <=-9223372036854775807 unchanged, and clamp finite negative overflow to-9223372036854775806. Saturating to INT64_MIN would produce the game's NaN sentinel and is incorrect. Read-only helper/output: `/tmp/zoot-fp-sentinels.py`.

## Class tail accessor evidence

Additional checked words in the cached current ELF (same SHA256 above, first PT_LOAD has VA=file offset):

| ELF VA | word | meaning |
|---|---|---|
|0x23f9120|f9404e89|methods pointer at0x98|
|0x23f9124 / 0x23f9148|7942428a / 79424288|method_count at0x120|
|0x23f8c80|f9404289|fields pointer at0x80|
|0x23f8c84 / 0x23f8ca8|79424a8a / 79424a88|field_count at0x124|
|0x23f8f84|b940f800|instance_size at0xf8|
|0x23fbfc8|b9411400|flags at0x114|
|0x23996c4|b9411800|token at0x118|

These place the opaque extra4 bytes after token and before method_count; they do not justify shifting instance_size, flags or token. The role/name of the new bytes is unknown and is not used by the module.

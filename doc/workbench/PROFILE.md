# Engine profile: where the CPU goes (2026-09-24)

**Setup.** Linux headless engine (RelWithDebInfo, fork `master`), server `lemon` (i9-14900K, 32 threads, 125 GB). `tools/workbench/profile.sh <scenario>`: the scenario runs at max sim speed, so the simulation, not idle waiting, is the bottleneck; `perf record -F 499 --call-graph dwarf` attaches once the scenario starts. Callers from `tools/workbench/stack_callers.py`. Percentages are of busy (non-idle) samples unless noted.

| Scenario | Sim speed reached | ms per sim frame |
|---|---|---|
| `mass_move_5000` (5,000 units crossing the map) | 2.6x real time | ~12.6 |
| `big_battle` (large seeded battle) | 2.1x real time | ~15.8 |

## Big battles: weapons checking line of fire (~55%)

| Self time | Function | Called from |
|---|---|---|
| 19.8% | `CCollisionHandler::Intersect` | `DetectHit` <- `TraceRay` <- `CWeapon::HaveFreeLineOfFire` (57%), `TestTrajectoryCone` <- `CCannon::HaveFreeLineOfFire` (17%) |
| 8.7% | `CMatrix44f::operator*` | inside `Intersect` |
| 6.4% | `CCollisionHandler::DetectHit` | `TraceRay` from `TryTargetHeading` (36%) and `AutoTarget` (32%) |
| 4.8% | `CGround::LineGroundCol` | `TraceRay` from `HaveFreeLineOfFire` |
| 4.2% | `TraceRay::TraceRay` | `HaveFreeLineOfFire` |
| 3.6% | `float3::min` | inside `Intersect` |
| 3.6% | `CMatrix44f::InvertAffine` | inside `Intersect` |
| 3.4% | `CMatrix44f::Translate` | inside `DetectHit` |

Inclusive path: `SimFrame` -> `SlowUpdateUnits` (39%) -> fight command `ExecuteFight` -> `GetClosestValidTarget` -> `QueryUnits<Enemy_InLos_ValidTarget, ClosestUnit>` -> `IsValidTarget` -> `TryTarget` -> `HaveFreeLineOfFire` -> `TraceRay` (27%); plus weapon auto-targeting in `SlowUpdateWeapons` (14%).

**Reading:** every fighting unit looking for the closest shootable enemy ray-traces each candidate, and every ray-vs-object test rebuilds and inverts that object's transform matrix (`Translate`, `operator*`, `InvertAffine`: ~16% on their own).

## Large armies moving: unit collisions and obstacle avoidance (~34%)

| Self time | Function | Called from |
|---|---|---|
| 15.4% | `CQuadField::GetUnitsExact` | `CGroundMoveType::HandleUnitCollisions` (99.8%) |
| 9.6% | `CQuadField::GetSolidsExact` | `GetObstacleAvoidanceDir` <- `UpdateObstacleAvoidance` <- `UpdateTraversalPlan` (100%) |
| 4.7% | `CGroundMoveType::HandleUnitCollisions` | `HandleObjectCollisions` |
| 3.9% | `CGroundMoveType::GetObstacleAvoidanceDir` | `UpdateObstacleAvoidance` |
| 3.7% | `CUnitScript::TickAllAnims` | `CUnitScriptEngine::Tick` (unit animation scripts) |
| 2.8% | `CCamera::CalcViewPortCoordinates` | `CUnitDrawerData::UpdateUnitIconStateScreen` |
| 2.7% | `ModelUniformsStorage::GetObjOffset` | `CUnitDrawerData::Update` |
| ~2% | LOS status updates | `CUnitHandler::UpdateUnitLosStates` |

**Reading:** every moving unit asks the quad field for all units near it (collisions) and all solids ahead of it (avoidance) every frame; the query itself, not the collision maths, is the cost.

## Other observations

- **The network thread burns ~27-36% of a core in local games** (`CGameServer::Update`, `ServerReadNet`, `ProcessPacket`, `select`/`ioctl`), with no remote players.
- **The headless engine still runs drawing work**: unit icon states, model uniforms, `CProjectileDrawer::UpdateDrawFlags` (4.4% of busy in `big_battle`). Wasted work on a dedicated server or AI-training host; also a hint that these passes are costly on clients.
- **Thread-pool workers spin** (`moodycamel::ConcurrentQueue::try_dequeue`, `linux_signal::wait_for`) when idle: CPU and power, not frame time.
- **At normal speed this CPU keeps up easily** (5,000 units at exactly 30 sim frames/s); the desktop spends ~45 ms per sim frame on the same scenario.

## Candidate optimisations (to verify with the A/B workbench)

1. **Cache each object's collision-volume transform and its inverse once per sim frame** instead of per ray (targets ~16% of big-battle CPU; results bit-identical if the same matrix is reused).
2. **Closest-valid-target search: sort candidates by distance, ray-trace in that order, stop at the first valid one** (today every candidate is validated); same result as long as ties break the same way.
3. **Cheaper neighbour queries for collisions/avoidance**: reuse one query per unit per frame for both collision and avoidance, or a tighter spatial structure; the query, not the maths, is the cost.
4. **Skip draw-side passes in the headless engine**; profile them in the client.
5. **Let the server thread block** instead of polling in local games.

Synced changes (1-3) must pass the replay-determinism check before merging.

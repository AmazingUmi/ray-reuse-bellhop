# FP-2F Batch Report — Source / Receiver Generalization

> Batch: FP-2F（multisource + Cartesian irregular receiver，含 paired A/a/E）
> 日期：2026-08-29
> 基线：`5a71221`（HEAD，FP-2F 全部改动未提交于其上）
> Worklist：`doc/worklists/FP-2F_SOURCE_RECEIVER_GENERALIZATION_WORKLIST.md`

## A. 冻结 Scope（§0）

**做**：multisource（point source、`NSz ≥ 1`、per-source 独立 trace、TL/R/A/a/E）、
Cartesian irregular receiver（run type 第 5 位 `I`、paired `NRz == NRr`；Cartesian
Cerveny/GeoHat/GeoGaussian TL 与 Cartesian `G/B` A/a/E；SHD `PlotType='irregular '`）、
三执行模式 byte-identity。

**不做**：line source（parser 继续拒绝 `X`）、3D/N×2D、ray-centered irregular
（F2CPP 自身 out of scope）、frozen cache schema/指纹算法变更、broadband R、
机制可达但未 oracle 验证的组合（multisource × Q / × ray-centered、irregular × Q 等）。

## B. 任务完成状态

| Task | 等级 | 状态 | Reviewer |
|---|---|---|---|
| F01 model schema（layout/multisource/校验矩阵） | ADVANCED | DONE | PASS |
| F02 parser + PRT（NSz、`I` 接受、`X` 拒绝） | ADVANCED | DONE | PASS |
| F03 per-source frozen cache 泛化 | ADVANCED | DONE | PASS |
| F04 per-(frequency, source) product state + 三模式 | ADVANCED | DONE | PASS |
| F05 Cartesian influence/traversal `depthAt` 寻址 | ADVANCED | DONE | PASS |
| F06 writers 多源/irregular 布局（SHD/ARR/E/R） | ADVANCED | DONE | PASS |
| F07 共享 case 启用/新增 + 三方 validator | STANDARD | DONE | （STANDARD 按需；ADVANCED 层已全覆盖） |
| F08 三模式一致性 + 冻结基线 | STANDARD | DONE | （同上） |
| F09 文档同步 | SIMPLE | DONE | — |

## C. 关键设计决定（冻结）

1. `RayPathCache` schema 与 `contentFingerprint()` 算法零改动；语义收紧为
   "one cache = one source 的 launch fan"，solver 编排层持有
   `vector<RayPathCache>`；reuse 单位 = "(source, frozen fan)" 跨频复用。
2. parallel 频率 worker 只读 cache vector（const 引用）；逐频 product state
   per-(frequency, source)；无 frequency-local 状态写回 cache；无 global
   current frequency / shared mutable state。
3. `tracePassCount` = per-source fan trace 次数（`nonreuse = Nfreq×NSz`，
   `reuse/parallel = NSz`）；单源两频 `2/1/1` 语义不变。
4. 多源 fan planning 用 1500 m/s 参考速度（Origin `angleMod` 同构）；
   单源保持源局部声速路径。
5. ray-count 上限 `kMaximumRunRayCount=2'000'000` 与 F2CPP 同值同积同严格
   `>`，检查点前移到 model 构造期（拒绝集合精确等价）。
6. **Reference 语义偏差裁定（两处，均以 reference 为准）**：
   - CC/IC/SC irregular 时 Origin `InfluenceCervenyCart` 恒读 `Rz(1)`
     （`iz=1..NRz_per_range=1`，paired 逻辑从未编写），F2CPP 显式保留
     （`irregularReceiverDepth = receiverDepths.front()`）；RayReuse 同构实现。
     GeoHat/GeoGaussian Cartesian 才是真 paired `Rz(ir)`。
   - SHD `PlotType` 为 `CHARACTER(LEN=10)`（`'irregular '`/`'rectilin  '`），
     worklist 草稿的 "12 字符" 系笔误；E 产品 Origin/F2CPP 均无 binary 模式。

## D. Batch Acceptance（coordinator 亲自抽验，2026-08-29）

| Gate | 结果 |
|---|---|
| 隔离 Release clean build（`build/fp2f-accept`，Warnings-as-Errors） | 通过，0 warning |
| 全量 CTest | **40/40** |
| `uv run pytest` | **178 passed** |
| `uv run make -C test/standard_cases test-unit` | **163 tests OK** |
| 三方 batch（origin/f2cpp/rayreuse × single profile 全部 case） | **BATCH PASSED，222 组合**（volume_attenuation 2 case 对 rayreuse SKIP，属 FP-2H 范围） |
| 三模式 byte-identity（`multi_source_depths` broadband 1000/2000 Hz，双命令亲跑） | SHD 三模式逐字节一致，SHA-256 `0e62bbe0cd0082ce…`；Trace passes `6/3/3`；per-source fingerprint before==after（`3625827730432409847` / `12792067103283003378` / `5610685649423155114`，互异） |
| 冻结基线（munk_spline broadband 50/250 Hz reuse，亲跑） | fingerprint `1526667602348633172` before==after；SHD SHA-256 `74028065178ff80d`（不变） |
| F07 三方 validator（worker 运行，coordinator 以 222 组合 batch 复核） | `multi_source_depths` origin↔rayreuse TL 3.81e-05 dB、f2cpp↔rayreuse payload exact；R 双源 0.0 m；irregular payload exact；A/a 8 case 0 ULP；E 5 case 0.0 m |
| Git 边界 | `git diff --check` 干净；`Bellhop_origin/`、`Bellhop_F2CPP/` 零改动；改动仅 `Bellhop_RayReuse/` + `test/standard_cases/`；`results/` gitignored，无生成产品入库 |

## E. Oracle Case 清单（8 个）

启用：`multi_source_depths`（TL CC NSz=3）、`irregular_receiver_pairs`
（TL CC paired）、`ray_trace_vacuum_rigid`（R 双源）、`eigenray_geometric_hat`
（E 双源）、`arrival_geometric_gaussian_irregular`（A paired）。
新增：`arrival_multi_source`（A ASCII 双源）、`arrival_multi_source_binary`
（a binary 双源）、`eigenray_irregular_pairs`（E paired）。

## F. Known Limitations（不声明 parity）

- CC/IC/SC irregular 恒取 `Rz(1)`（reference legacy 语义，非 paired）；
- multisource × Q、multisource × ray-centered、irregular × Q 等机制可达
  但无独立 oracle 的组合不声明；
- line source（`X`）parser 拒绝，仍为 GAP；3D/N×2D 不支持；
- R 保持单频、不接受 execution mode。

## G. 结论

```text
FP-2F BATCH ACCEPTANCE: PASS
```

（正式批次结论由独立 final-reviewer 给出；本报告不声明 `FP-2F ACCEPTED`。）

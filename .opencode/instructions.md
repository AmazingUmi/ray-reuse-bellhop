# OpenCode Execution Policy

This file supplements the repository-root `AGENTS.md`.
The Codex worklist, repository rules, source code, tests, validation reports,
and Git history are authoritative. Chat summaries are not authoritative.

## Workflow

- External GPT/Codex is the architect and final batch reviewer.
- GPT/Codex provides a bounded worklist with architecture decisions, task scope,
  dependencies, and acceptance criteria.
- `general-worker` is the default OpenCode executor.
- `advanced-worker` is used only for difficult or high-risk implementation and debugging.
- OpenCode must not redesign architecture that the Codex worklist has already frozen.

## Provider Policy

- General work MUST use Vertex `google-vertex/gemini-3.7-flash`.
- Advanced work MUST use OpenCode Go `opencode-go/glm-5.3`.
- Do not use DeepSeek models unless the task explicitly overrides this policy.

## Routing Policy

- `[GENERAL]` tasks are executed directly by `general-worker`.
- `[ADVANCED]` tasks are delegated by `general-worker` to `advanced-worker`.
- If a task is not labeled, treat it as `[GENERAL]` by default.
- Do not call `advanced-worker` merely to re-read, summarize, plan, run routine tests,
  make mechanical edits, update documentation, or perform straightforward single-module work.

Escalate an unlabeled task to `advanced-worker` only when one or more of these apply:

- cross-module core behavior or public API changes;
- ownership, lifetime, cache consistency, concurrency, or synchronization;
- numerical/scientific semantics where subtle correctness matters;
- performance work requiring nontrivial profiling or algorithmic changes;
- difficult debugging after one well-scoped General attempt fails;
- conflicting regressions or unclear failure causality;
- the Codex worklist explicitly marks the task high-risk or Advanced.

The General worker must provide the Advanced worker with a bounded task containing:
objective, allowed scope/files, frozen decisions, acceptance criteria, and relevant evidence.
The Advanced worker must not delegate further.

## Execution Policy

- Work only on the explicitly assigned Codex batch or task.
- Respect task dependencies. Parallelize only tasks that are independent and do not edit
  overlapping files or shared mutable state.
- Do not reopen frozen iterations or expand scope without an explicit blocker report.
- Do not add speculative abstractions, unrelated refactors, broad formatting, or test-framework
  expansion.
- After delegated work, `general-worker` must inspect the actual working tree and test results;
  subagent reports are not accepted as evidence by themselves.
- Run the minimum required focused tests during tasks and the required batch tests before handoff.

## Handoff to Codex

When the batch is complete, `general-worker` must stop and report:

1. completed tasks and which model/worker handled each one;
2. changed files and implementation summary;
3. exact test commands and results;
4. deviations from the Codex worklist;
5. unresolved issues, risks, or recommended follow-up;
6. current Git working-tree status.

The final architecture/correctness review and commit decision belong to external GPT/Codex.

## Git Policy

- OpenCode agents must not stage files, create commits or tags, push, merge, rebase,
  switch branches, or rewrite Git history.
- Leave implementation changes unstaged for GPT/Codex review and acceptance.
- Never use destructive Git or broad cleanup commands.

# OpenCode Agent Policy

This file supplements the repository-root `AGENTS.md`. The repository rules,
task documents, source code, tests, validation reports, and Git history are the
authoritative project state. Chat summaries are not authoritative.

## Roles

- `lead` is responsible for task decomposition, delegation, integration, and verification.
- `worker` handles normal implementation tasks.
- `worker-hard` handles difficult implementation, debugging, or cross-module tasks.

## Provider Policy

- GLM models MUST use OpenCode Go.
- The lead agent MUST use `opencode-go/glm-5.3`.
- DeepSeek workers MUST use the official DeepSeek provider.
- Normal workers MUST use `deepseek/deepseek-v4-flash`.
- Difficult workers MAY use `deepseek/deepseek-v4-pro`.
- Do NOT use `opencode-go/deepseek-*`.
- Do NOT use `opencode/deepseek-v4-flash-free` for project implementation.

## Delegation Policy

- The lead should delegate implementation work whenever tasks can be isolated.
- Workers must stay within their assigned task scope.
- Workers must not recursively create additional workers.
- The lead must inspect worker results before integration.
- The lead must run the required tests after integration.
- Final architecture and correctness review is performed externally by GPT/Codex.

## Scope and Handoff Policy

- Work only on the explicitly assigned batch or task.
- Do not reopen frozen iterations or modify `Bellhop_RayReuse` while working on
  `Bellhop_F2CPP` (or vice versa) unless the task explicitly authorizes it.
- Do not expand the test framework, add speculative abstractions, or perform
  unrelated refactoring or formatting.
- Workers must stop after their assigned task and report modified files,
  implementation details, actual test commands/results, deviations, and
  unresolved issues.
- The lead must verify the actual working tree and tests; worker reports are not
  accepted as evidence by themselves.

## Git Policy

- OpenCode agents must not stage files, create commits or tags, push, merge,
  rebase, switch branches, or rewrite Git history.
- Leave all implementation changes unstaged for GPT/Codex review, acceptance,
  task-status updates, and commit creation.
- Never use destructive Git or broad cleanup commands.

@AGENTS.md

## Claude Code specifics

- **Memory is not authoritative.** Auto-memory and this file can be stale. Before a consequential decision, cite the current file or command output, not recalled context.
- Permission precedence is deny > ask > allow. Keep allow rules narrow.
- Use subagents only after the single-agent loop works.

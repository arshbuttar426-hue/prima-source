/*
 * plugin.ts — "prima-frame-check"
 *
 * An unusual opencode plugin: it brings prima's as/not_as judgment model into
 * the session so the agent can decide, with the language's exact rules,
 * whether a value "counts as" something.
 *
 * Built fully isolated — nothing here touches the live opencode config.
 * To test in isolation, load it with:
 *
 *   OPENCODE_CONFIG=/abs/path/prima-frame-check/opencode.json opencode
 *
 * The plugin is deliberately odd: it judges. Not files, not types —
 * values, against frames, using the same semantics as prima.c:
 *
 *   x as FRAME      both sides enforced
 *   x not_as FRAME  positive side only
 *
 * Tools:
 *   prima_define  — define once (prima syntax), swap freely
 *   prima_judge   — point at a frame, get the verdict
 *   prima_frames  — the registry, visible
 *   prima_watch   — judge every tool result against a frame from now on
 */

import { tool } from "@opencode-ai/plugin";
import type { Plugin } from "@opencode-ai/plugin";
import {
  FrameRegistry,
  fromJson,
  judgeAs,
  judgeNotAs,
  loadFrames,
  parseFrameDefs,
  valToString,
  verdictLine,
} from "./frame-engine.ts";

const DEFAULT_FRAMES = `
fav_movie as string not_as digit;
single_in as alpha not_as empty;
batch_in as string not_as single_in;
passing_score as number not_as negative;
perfect_score as number not_as nonzero;
non_empty as string not_as empty;
`;

const INLINE_JUDGMENT = /^([A-Za-z_][A-Za-z0-9_<>]*)\s+not_as\s+([A-Za-z_][A-Za-z0-9_<>]*)$/;

const plugin: Plugin = async () => {
  const registry = new FrameRegistry();
  loadFrames(registry, DEFAULT_FRAMES);

  let watchFrame: string | undefined;

  const parseInline = (frame: string): string => {
    // inline judgment form: "POS not_as NEG" -> an anonymous frame
    const m = INLINE_JUDGMENT.exec(frame.trim());
    if (m) {
      const name = `_inline_${m[1]}`;
      registry.define(name, m[1], m[2]);
      return name;
    }
    return frame;
  };

  return {
    tool: {
      prima_define: tool({
        description:
          "Define or redefine a prima frame, written in prima syntax like 'fav_movie as string not_as digit;'. " +
          "Defining once lets every later judgment point at the name; redefining swaps the judgment instantly.",
        args: {
          frame_def: {
            description: "A prima frame definition, e.g. 'score as number not_as negative;'",
            type: "string",
          },
        },
        async execute(args) {
          const defs = parseFrameDefs(args.frame_def);
          if (defs.length === 0) {
            return `prima: could not parse frame definition — expected 'name as POS not_as NEG;'`;
          }
          const out: string[] = [];
          for (const d of defs) {
            registry.define(d.name, d.pos, d.neg);
            const f = registry.get(d.name)!;
            out.push(
              `defined: ${d.name} as ${d.pos} not_as ${d.neg}` +
                `  (positive = ${f.posName}'s predicate, negative = ${f.negName}'s positive)`,
            );
          }
          return out.join("\n");
        },
      }),

      prima_judge: tool({
        description:
          "Judge a value against a prima frame. Frame may be a registered name (e.g. 'fav_movie') or an inline " +
          "judgment in prima syntax (e.g. 'string not_as empty'). Use not_as=true to apply the negative primitive. " +
          "The verdict follows prima exactly: 'as' enforces both sides, 'not_as' enforces the positive only.",
        args: {
          target: {
            description: "Name to attach to the judgment (the x in 'x as frame').",
            type: "string",
          },
          value: {
            description: "The value being judged.",
          },
          frame: {
            description: "A registered frame name or inline 'POS not_as NEG' judgment.",
            type: "string",
          },
          not_as: {
            description: "Apply the 'not_as' primitive instead of 'as' (default false).",
            type: "boolean",
          },
        },
        async execute(args) {
          const frameName = parseInline(args.frame);
          const value = fromJson(args.value);
          const verdict = args.not_as
            ? judgeNotAs(args.target, value, frameName, registry)
            : judgeAs(args.target, value, frameName, registry);
          const lines = [
            `value: ${valToString(value)}`,
            `frame: ${args.frame}`,
            `verdict: ${verdictLine(verdict)}`,
          ];
          if (verdict.pass) {
            lines.push(`  ✓ ${args.target} counts as ${args.frame} (positive ✓ negative ✓)`);
          }
          return lines.join("\n");
        },
      }),

      prima_frames: tool({
        description:
          "List every registered prima frame (built-in and user-defined) and how each one composes positive/negative.",
        args: {
          filter: {
            description: "Optional name filter; list only frames containing this text.",
            type: "string",
          },
        },
        async execute(args) {
          const frames = registry
            .list()
            .filter((f) => !args.filter || f.name.includes(args.filter));
          if (frames.length === 0) return `no frames match '${args.filter}'`;
          const lines = frames.map((f) => {
            const tag = f.userDefined ? "user" : "builtin";
            return `${f.name.padEnd(16)} ${tag.padEnd(8)} as ${f.posName.padEnd(10)} not_as ${f.negName}`;
          });
          return `${frames.length} frames:\n` + lines.join("\n");
        },
      }),

      prima_watch: tool({
        description:
          "Register a frame to judge every tool result from now on. " +
          "Pass a frame name or inline judgment, or an empty string to stop watching. " +
          "Demonstrates define-once, point-forever on live tool output.",
        args: {
          frame: {
            description: "Frame name, inline 'POS not_as NEG', or '' to stop.",
            type: "string",
          },
        },
        async execute(args) {
          const trimmed = (args.frame ?? "").trim();
          if (!trimmed) {
            watchFrame = undefined;
            return "prima_watch: stopped — tool results are no longer judged.";
          }
          const frameName = parseInline(trimmed);
          watchFrame = frameName;
          return `prima_watch: every tool result will be judged as '${trimmed}' from now on.`;
        },
      }),
    },

    "tool.execute.after": async (input, output) => {
      if (!watchFrame) return;
      const value = fromJson(output.output);
      const verdict = judgeAs("tool.output", value, watchFrame, registry);
      const note = verdict.pass
        ? `✓ tool output counts as '${watchFrame}'`
        : `✗ ${verdict.message}`;
      output.output = `${output.output}\n\n<prima-verdict>${note}</prima-verdict>`;
    },
  };
};

export default plugin;

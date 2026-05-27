import '../models/block_type.dart';
import '../models/block_configuration.dart';
import '../models/configuration_rules.dart';

// ─────────────────────────────────────────────────────────────────────────────
// Data model returned to the UI
// ─────────────────────────────────────────────────────────────────────────────

enum SuggestionKind {
  questInProgress,  // actively working on a quest
  questComplete,    // all steps done, chain is valid
  correction,       // chain has violations — corrective hint
  freeBuildSuccess, // free-build mode, chain is valid
  noBlocks,         // only Brain or empty
}

class BlockSuggestion {
  final SuggestionKind kind;

  /// Quest metadata (valid for questInProgress / questComplete)
  final String questName;
  final String questEmoji;
  final int questIndex;       // 0-based quest number
  final int completedSteps;   // how many template positions are done
  final int totalSteps;       // total positions (excluding Brain)

  /// Primary display text
  final String headline;
  final String body;

  /// The block type the kid should snap on next (may be null)
  final BlockType? nextBlockType;

  /// Suggests next quest after completing one
  final String? nextQuestName;

  const BlockSuggestion({
    required this.kind,
    this.questName = '',
    this.questEmoji = '',
    this.questIndex = 0,
    this.completedSteps = 0,
    this.totalSteps = 1,
    required this.headline,
    required this.body,
    this.nextBlockType,
    this.nextQuestName,
  });

  double get progressFraction =>
      totalSteps > 0 ? completedSteps / totalSteps : 0.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal quest template
// ─────────────────────────────────────────────────────────────────────────────

class _QuestStep {
  /// Exact block type to match, or null meaning "any output block"
  final BlockType? exact;
  final String headline;
  final String body;

  const _QuestStep({this.exact, required this.headline, required this.body});
}

class _Quest {
  final int index;
  final String name;
  final String emoji;
  final List<_QuestStep> steps; // does NOT include the Brain Block at position 0

  const _Quest({
    required this.index,
    required this.name,
    required this.emoji,
    required this.steps,
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// The 4 quests
// ─────────────────────────────────────────────────────────────────────────────

const List<_Quest> _quests = [
  // Quest 1 — Sequence: Brain + any output
  _Quest(
    index: 0,
    name: 'Make Something Happen!',
    emoji: '⚡',
    steps: [
      _QuestStep(
        exact: null, // any output
        headline: 'Great start!',
        body: 'Snap on a NOTE or LED block — make something happen!',
      ),
    ],
  ),

  // Quest 2 — Condition: Brain → If → ButtonPress → Then → output → EndIf
  _Quest(
    index: 1,
    name: 'Use a Button!',
    emoji: '🔘',
    steps: [
      _QuestStep(
        exact: BlockType.ifBlock,
        headline: 'Nice start!',
        body: 'Snap on an IF block — it asks a question!',
      ),
      _QuestStep(
        exact: BlockType.buttonPress,
        headline: 'Getting there!',
        body: 'Now add a BUTTON PRESS block — it listens for your push!',
      ),
      _QuestStep(
        exact: BlockType.thenBlock,
        headline: 'Almost there!',
        body: 'Add a THEN block — this is where the magic happens!',
      ),
      _QuestStep(
        exact: null, // any output
        headline: 'One more!',
        body: 'Add a NOTE or LED block to make something cool!',
      ),
      _QuestStep(
        exact: BlockType.endIfBlock,
        headline: 'Last step!',
        body: 'Snap on the END IF block to finish your program!',
      ),
    ],
  ),

  // Quest 3 — Loop: Brain → Loop → output → EndLoop
  _Quest(
    index: 2,
    name: 'Repeat Forever!',
    emoji: '🔄',
    steps: [
      _QuestStep(
        exact: BlockType.loopBlock,
        headline: 'Loop time!',
        body: 'Snap on a LOOP block — it repeats forever!',
      ),
      _QuestStep(
        exact: null, // any output
        headline: 'Getting closer!',
        body: 'Add a NOTE or LED block — this will repeat over and over!',
      ),
      _QuestStep(
        exact: BlockType.endLoopBlock,
        headline: 'Almost done!',
        body: 'Snap on END LOOP to close the loop!',
      ),
    ],
  ),

  // Quest 4 — Combined: Brain → Loop → If → ButtonPress → Then → output → EndIf → EndLoop
  _Quest(
    index: 3,
    name: 'The Whole Thing!',
    emoji: '🏆',
    steps: [
      _QuestStep(
        exact: BlockType.loopBlock,
        headline: 'Boss level!',
        body: 'Start with a LOOP block — everything goes inside!',
      ),
      _QuestStep(
        exact: BlockType.ifBlock,
        headline: 'Step 2!',
        body: 'Now add an IF block inside the loop!',
      ),
      _QuestStep(
        exact: BlockType.buttonPress,
        headline: 'Step 3!',
        body: 'Add the BUTTON PRESS block — it listens for you!',
      ),
      _QuestStep(
        exact: BlockType.thenBlock,
        headline: 'Halfway there!',
        body: 'Add a THEN block — the action goes here!',
      ),
      _QuestStep(
        exact: null, // any output
        headline: 'Almost done!',
        body: 'Add a NOTE or LED block for the action!',
      ),
      _QuestStep(
        exact: BlockType.endIfBlock,
        headline: 'Two more!',
        body: 'Close the If with an END IF block!',
      ),
      _QuestStep(
        exact: BlockType.endLoopBlock,
        headline: 'Final step!',
        body: 'Close everything with END LOOP — you\'re a legend!',
      ),
    ],
  ),
];

// ─────────────────────────────────────────────────────────────────────────────
// Violation → kid-friendly corrective hint
// ─────────────────────────────────────────────────────────────────────────────

String _violationHint(RuleViolation v) {
  switch (v.type) {
    case RuleViolationType.brainBlockMissing:
      return 'The Boss Block is missing! Make sure the Brain Block is connected first.';
    case RuleViolationType.brainBlockNotFirst:
      return 'The Boss Block needs to be first in line! Move the Brain Block to the front.';
    case RuleViolationType.buttonPressMustFollowIf:
      return 'The Button block needs to come right after an If block!';
    case RuleViolationType.thenMustImmediatelyFollowButton:
      return 'Put the Then block right after the Button block!';
    case RuleViolationType.ifSequenceIncomplete:
      return 'Your If block needs to be closed — add an End If block at the end!';
    case RuleViolationType.ifSequenceInvalidBlock:
      return 'Your If block setup is off — make sure the order is: If → Button → Then → (output) → End If!';
    case RuleViolationType.loopSequenceIncomplete:
      return 'Your Loop block needs to be closed — add an End Loop block!';
    case RuleViolationType.loopSequenceInvalidBlock:
      return 'Something\'s off inside the Loop — try: Loop → If → Button → Then → (output) → End If → End Loop!';
    case RuleViolationType.sequenceInterleaved:
      return 'Your blocks are tangled — finish one group before starting another!';
    case RuleViolationType.unmatchedEnd:
      return 'There\'s an extra closing block with no partner — check the order!';
    case RuleViolationType.emptySequence:
      return 'Add a Note or LED block inside so something actually happens!';
    case RuleViolationType.duplicateI2cAddress:
      return 'Two blocks seem confused — they\'re sharing the same address. Try swapping one out.';
    case RuleViolationType.nestingOverflow:
      return 'Too many blocks are nested — try simplifying your program!';
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Service
// ─────────────────────────────────────────────────────────────────────────────

class BlockSuggestionService {
  /// Returns the single most relevant hint for the current chain state.
  ///
  /// [chain] — the current ordered list of connected blocks (Brain at index 0).
  /// [violations] — already-computed validator output.
  /// [freeBuildMode] — when true, skip quest matching and show validator-only hints.
  /// [highestQuestCompleted] — unlocks "try quest N+1" nudge.
  static BlockSuggestion getSuggestion({
    required List<BlockInfo> chain,
    required List<RuleViolation> violations,
    bool freeBuildMode = false,
    int highestQuestCompleted = -1,
  }) {
    final blockTypes = chain.map((b) => b.blockType).toList();

    // ── Free Build mode ──────────────────────────────────────────────────────
    if (freeBuildMode) {
      if (violations.isNotEmpty) {
        final v = _priorityViolation(violations);
        return BlockSuggestion(
          kind: SuggestionKind.correction,
          headline: 'Heads up! 🔧',
          body: _violationHint(v),
        );
      }
      return const BlockSuggestion(
        kind: SuggestionKind.freeBuildSuccess,
        headline: 'Looks great! 🚀',
        body: 'Hit RUN on the Brain Block and watch it go!',
      );
    }

    // ── No blocks (just Brain or empty) ─────────────────────────────────────
    if (blockTypes.isEmpty ||
        (blockTypes.length == 1 && blockTypes.first == BlockType.brainBlock)) {
      return const BlockSuggestion(
        kind: SuggestionKind.noBlocks,
        questName: 'Make Something Happen!',
        questEmoji: '⚡',
        questIndex: 0,
        completedSteps: 0,
        totalSteps: 1,
        headline: 'Ready to build!',
        body: 'The Boss Block is connected! Now snap on a NOTE or LED block to make something happen!',
        nextBlockType: null,
      );
    }

    // ── Score each quest ─────────────────────────────────────────────────────
    int bestScore = 0;
    _Quest? bestQuest;

    for (final quest in _quests) {
      final score = _scoreQuest(blockTypes, quest);
      if (score > bestScore) {
        bestScore = score;
        bestQuest = quest;
      }
    }

    // ── Quest in progress ────────────────────────────────────────────────────
    if (bestQuest != null && bestScore > 0) {
      final isComplete = bestScore > bestQuest.steps.length;

      if (isComplete) {
        // Quest template fully matched
        if (violations.isNotEmpty) {
          // Complete but has errors (e.g., extra blocks appended)
          final v = _priorityViolation(violations);
          return BlockSuggestion(
            kind: SuggestionKind.correction,
            questName: bestQuest.name,
            questEmoji: bestQuest.emoji,
            questIndex: bestQuest.index,
            completedSteps: bestQuest.steps.length,
            totalSteps: bestQuest.steps.length,
            headline: 'Almost perfect! 🔧',
            body: _violationHint(v),
          );
        }
        // Truly complete and valid
        final nextQuestName = bestQuest.index < _quests.length - 1
            ? _quests[bestQuest.index + 1].name
            : null;
        return BlockSuggestion(
          kind: SuggestionKind.questComplete,
          questName: bestQuest.name,
          questEmoji: bestQuest.emoji,
          questIndex: bestQuest.index,
          completedSteps: bestQuest.steps.length,
          totalSteps: bestQuest.steps.length,
          headline: 'Quest complete! 🎉',
          body: nextQuestName != null
              ? 'Amazing job! Ready for the next challenge?'
              : 'You\'ve built the whole thing — you\'re a real programmer!',
          nextQuestName: nextQuestName,
        );
      }

      // Quest in progress — show next step
      final stepIdx = bestScore - 1; // how many steps done (0-based)
      final nextStep = bestQuest.steps[stepIdx];
      return BlockSuggestion(
        kind: SuggestionKind.questInProgress,
        questName: bestQuest.name,
        questEmoji: bestQuest.emoji,
        questIndex: bestQuest.index,
        completedSteps: stepIdx,
        totalSteps: bestQuest.steps.length,
        headline: nextStep.headline,
        body: nextStep.body,
        nextBlockType: nextStep.exact,
      );
    }

    // ── No quest match — fall back to validator hints ────────────────────────
    if (violations.isNotEmpty) {
      final v = _priorityViolation(violations);
      return BlockSuggestion(
        kind: SuggestionKind.correction,
        headline: 'Hmm… 🔧',
        body: _violationHint(v),
      );
    }

    // Valid chain but doesn't match any quest pattern
    return const BlockSuggestion(
      kind: SuggestionKind.freeBuildSuccess,
      headline: 'Looks great! 🚀',
      body: 'Hit RUN on the Brain Block and watch it go!',
    );
  }

  /// Counts how many consecutive template positions (including Brain at 0)
  /// are satisfied by the current chain from the start.
  /// Returns value > quest.steps.length when the full quest template is matched.
  static int _scoreQuest(List<BlockType?> chain, _Quest quest) {
    // Template is: [Brain] + quest.steps
    // Position 0 must be brainBlock
    if (chain.isEmpty || chain[0] != BlockType.brainBlock) return 0;

    int score = 1; // Brain matched
    for (int i = 0; i < quest.steps.length; i++) {
      final chainIdx = i + 1;
      if (chainIdx >= chain.length) break;

      final step = quest.steps[i];
      final chainBlock = chain[chainIdx];

      if (step.exact != null) {
        if (chainBlock != step.exact) break;
      } else {
        // Any output block
        if (!(chainBlock?.isOutput ?? false)) break;
      }
      score++;
    }
    return score;
  }

  /// Returns the highest-priority violation to show (errors before warnings,
  /// then the first in the list).
  static RuleViolation _priorityViolation(List<RuleViolation> violations) {
    final errors = violations.where((v) => v.severity == Severity.error).toList();
    return errors.isNotEmpty ? errors.first : violations.first;
  }
}

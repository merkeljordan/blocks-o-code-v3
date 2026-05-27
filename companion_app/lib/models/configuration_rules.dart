import 'block_type.dart';
import 'block_configuration.dart';

/// Represents a rule violation with severity
class RuleViolation {
  final RuleViolationType type;
  final String message;
  final Severity severity;
  final int? blockIndex;
  final BlockType? expectedBlockType;
  final BlockType? actualBlockType;
  final List<int>? affectedBlockIndices;

  RuleViolation({
    required this.type,
    required this.message,
    required this.severity,
    this.blockIndex,
    this.expectedBlockType,
    this.actualBlockType,
    this.affectedBlockIndices,
  });

  @override
  String toString() {
    return 'RuleViolation(type: $type, severity: $severity, message: $message, blockIndex: $blockIndex)';
  }
}

/// Types of rule violations
enum RuleViolationType {
  brainBlockMissing,
  brainBlockNotFirst,
  buttonPressMustFollowIf,
  thenMustImmediatelyFollowButton,
  duplicateI2cAddress,
  ifSequenceIncomplete,
  ifSequenceInvalidBlock,
  loopSequenceIncomplete,
  loopSequenceInvalidBlock,
  sequenceInterleaved,
  nestingOverflow,
  unmatchedEnd,
  emptySequence,
}

/// Severity of a rule violation
enum Severity {
  error,
  warning,
}

/// Container for all configuration rules
class ConfigurationRules {
  /// Check if Brain Block is at position 0 and present
  static List<RuleViolation> checkBrainBlockRule(BlockConfiguration config) {
    final violations = <RuleViolation>[];

    // Check if Brain Block exists
    final brainBlocks = config.getBlocksByType(BlockType.brainBlock);
    if (brainBlocks.isEmpty) {
      violations.add(RuleViolation(
        type: RuleViolationType.brainBlockMissing,
        message: 'The Brain Block is missing! Make sure the Brain Block is connected first.',
        severity: Severity.error,
      ));
      return violations;
    }

    // Check if Brain Block is at position 0
    if (config.blocks.isNotEmpty) {
      final firstBlock = config.blocks[0];
      if (firstBlock.blockType != BlockType.brainBlock) {
        violations.add(RuleViolation(
          type: RuleViolationType.brainBlockNotFirst,
          message: 'The Brain Block needs to be first in line! Move the Brain Block to the front.',
          severity: Severity.error,
          blockIndex: 0,
          expectedBlockType: BlockType.brainBlock,
          actualBlockType: firstBlock.blockType,
        ));
      }
    }

    return violations;
  }

  /// Every Button Press must be immediately preceded by an If Block (canonical
  /// `IF → BUTTON → THEN` binding matches firmware `program[if_pc + 1]`).
  static List<RuleViolation> checkButtonPressPlacementRule(
      BlockConfiguration config) {
    final violations = <RuleViolation>[];
    final blocks = config.blocks;

    for (int i = 0; i < blocks.length; i++) {
      if (blocks[i].blockType != BlockType.buttonPress) continue;

      final prev = i > 0 ? blocks[i - 1].blockType : null;
      if (prev != BlockType.ifBlock) {
        violations.add(RuleViolation(
          type: RuleViolationType.buttonPressMustFollowIf,
          message: prev == null
              ? 'The Button block at position $i needs an If block right before it!'
              : 'The Button block at position $i must come right after an If block — not a ${prev.displayName} block!',
          severity: Severity.error,
          blockIndex: i,
          expectedBlockType: BlockType.ifBlock,
          actualBlockType: prev,
        ));
      }
    }

    return violations;
  }

  /// Canonical tutorial shape: `Button Press` must be immediately followed by
  /// `Then` (stricter than firmware, which allows other opcodes in between).
  static List<RuleViolation> checkThenImmediatelyFollowsButtonRule(
      BlockConfiguration config) {
    final violations = <RuleViolation>[];
    final blocks = config.blocks;

    for (int i = 0; i < blocks.length; i++) {
      if (blocks[i].blockType != BlockType.buttonPress) continue;

      if (i + 1 >= blocks.length) {
        violations.add(RuleViolation(
          type: RuleViolationType.thenMustImmediatelyFollowButton,
          message:
              'The Button block at position $i needs a Then block right after it!',
          severity: Severity.error,
          blockIndex: i,
          expectedBlockType: BlockType.thenBlock,
        ));
        continue;
      }

      final next = blocks[i + 1].blockType;
      if (next != BlockType.thenBlock) {
        violations.add(RuleViolation(
          type: RuleViolationType.thenMustImmediatelyFollowButton,
          message:
              'The Button block at position $i needs a Then block right after it — not a ${next?.displayName ?? "unknown"} block!',
          severity: Severity.error,
          blockIndex: i,
          expectedBlockType: BlockType.thenBlock,
          actualBlockType: next,
        ));
      }
    }

    return violations;
  }

  /// Two blocks must not share the same I²C address.
  static List<RuleViolation> checkDuplicateI2cRule(
      BlockConfiguration config) {
    final violations = <RuleViolation>[];
    final byAddr = <int, List<int>>{};

    for (int i = 0; i < config.blocks.length; i++) {
      final addr = config.blocks[i].i2cAddress;
      byAddr.putIfAbsent(addr, () => []).add(i);
    }

    for (final entry in byAddr.entries) {
      if (entry.value.length < 2) continue;
      final indices = List<int>.from(entry.value)..sort();
      violations.add(RuleViolation(
        type: RuleViolationType.duplicateI2cAddress,
        message:
            'Two blocks at positions ${indices.join(", ")} seem confused — they\'re sharing the same address. Try swapping one out.',
        severity: Severity.error,
        affectedBlockIndices: indices,
      ));
    }

    return violations;
  }

  /// Validate If/Loop nesting and structure using a stack-based approach
  static List<RuleViolation> checkNestingRule(BlockConfiguration config) {
    final violations = <RuleViolation>[];
    final blocks = config.blocks;
    final stack = <_NestingFrame>[];

    for (int i = 0; i < blocks.length; i++) {
      final block = blocks[i];
      final type = block.blockType;

      if (type == BlockType.ifBlock) {
        stack.add(_NestingFrame(type: type!, startIndex: i));
      } else if (type == BlockType.loopBlock) {
        stack.add(_NestingFrame(type: type!, startIndex: i));
      } else if (type == BlockType.thenBlock) {
        if (stack.isEmpty) {
          violations.add(RuleViolation(
            type: RuleViolationType.unmatchedEnd,
            message: 'The Then block at position $i doesn\'t have an If block before it — Then always needs an If!',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else if (stack.last.type == BlockType.loopBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.loopSequenceInvalidBlock,
            message:
                'The Then block at position $i is inside a Loop — add an If block and Button block before the Then block!',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else if (stack.last.type != BlockType.ifBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.unmatchedEnd,
            message: 'The Then block at position $i doesn\'t have an If block before it — Then always needs an If!',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else if (stack.last.hasThen) {
          violations.add(RuleViolation(
            type: RuleViolationType.ifSequenceInvalidBlock,
            message: 'The If block at position ${stack.last.startIndex} already has a Then block — each If can only have one Then!',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else {
          // Firmware binds BUTTON at program[if_pc + 1] (must be before THEN).
          final ifStart = stack.last.startIndex;
          final afterIf = ifStart + 1;
          if (afterIf >= i ||
              blocks[afterIf].blockType != BlockType.buttonPress) {
            violations.add(RuleViolation(
              type: RuleViolationType.ifSequenceInvalidBlock,
              message: 'The If block at position $ifStart needs a Button block right after it — before the Then block!',
              severity: Severity.error,
              blockIndex: ifStart,
              expectedBlockType: BlockType.buttonPress,
              actualBlockType:
                  afterIf < i ? blocks[afterIf].blockType : null,
            ));
          }
          stack.last.hasThen = true;
        }
      } else if (type == BlockType.endIfBlock) {
        if (stack.isEmpty) {
          violations.add(RuleViolation(
            type: RuleViolationType.unmatchedEnd,
            message: 'The End If block at position $i doesn\'t have a matching If block to close!',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else if (stack.last.type == BlockType.loopBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.sequenceInterleaved,
            message:
                'The End If block at position $i needs to come after End Loop — finish the Loop first!',
            severity: Severity.warning,
            blockIndex: i,
          ));
        } else if (stack.last.type != BlockType.ifBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.unmatchedEnd,
            message: 'The End If block at position $i doesn\'t have a matching If block to close!',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else {
          final frame = stack.removeLast();
          if (!frame.hasThen) {
            violations.add(RuleViolation(
              type: RuleViolationType.ifSequenceIncomplete,
              message: 'The If block at position ${frame.startIndex} is missing its Then block — add one to complete the pair!',
              severity: Severity.error,
              blockIndex: frame.startIndex,
            ));
          }
          if (!frame.hasContent(i)) {
            violations.add(RuleViolation(
              type: RuleViolationType.emptySequence,
              message: 'The If block at position ${frame.startIndex} doesn\'t do anything yet — add a Note or LED block inside!',
              severity: Severity.warning,
              blockIndex: frame.startIndex,
            ));
          }
        }
      } else if (type == BlockType.endLoopBlock) {
        if (stack.isEmpty) {
          violations.add(RuleViolation(
            type: RuleViolationType.unmatchedEnd,
            message: 'The End Loop block at position $i doesn\'t have a matching Loop block to close!',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else if (stack.last.type == BlockType.ifBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.sequenceInterleaved,
            message:
                'The End Loop block at position $i needs to come after End If — finish the If block first!',
            severity: Severity.warning,
            blockIndex: i,
          ));
        } else if (stack.last.type != BlockType.loopBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.unmatchedEnd,
            message: 'The End Loop block at position $i doesn\'t have a matching Loop block to close!',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else {
          final frame = stack.removeLast();
          if (!frame.hasContent(i)) {
            violations.add(RuleViolation(
              type: RuleViolationType.emptySequence,
              message: 'The Loop block at position ${frame.startIndex} doesn\'t do anything yet — add a Note or LED block inside!',
              severity: Severity.warning,
              blockIndex: frame.startIndex,
            ));
          }
        }
      } else if (type?.isOutput ?? false) {
        for (var frame in stack) {
          frame.contentCount++;
        }
      }
    }

    // Check for unclosed sequences
    while (stack.isNotEmpty) {
      final frame = stack.removeLast();
      violations.add(RuleViolation(
        type: (frame.type == BlockType.ifBlock)
            ? RuleViolationType.ifSequenceIncomplete
            : RuleViolationType.loopSequenceIncomplete,
        message: 'The ${frame.type == BlockType.ifBlock ? "If" : "Loop"} block at position ${frame.startIndex} needs to be closed — add an ${frame.type == BlockType.ifBlock ? "End If" : "End Loop"} block at the end!',
        severity: Severity.error,
        blockIndex: frame.startIndex,
      ));
    }

    return violations;
  }

  /// Validate all rules against a configuration
  static List<RuleViolation> validateAll(BlockConfiguration config) {
    final violations = <RuleViolation>[];

    violations.addAll(checkBrainBlockRule(config));
    violations.addAll(checkDuplicateI2cRule(config));

    // Button Press must sit immediately after If (global; pairs with nesting)
    violations.addAll(checkButtonPressPlacementRule(config));
    violations.addAll(checkThenImmediatelyFollowsButtonRule(config));

    // Validate nesting (IF/LOOP) and structural integrity
    violations.addAll(checkNestingRule(config));

    return violations;
  }
}

class _NestingFrame {
  final BlockType type;
  final int startIndex;
  bool hasThen = false;
  int contentCount = 0;

  _NestingFrame({required this.type, required this.startIndex});

  bool hasContent(int endIndex) => contentCount > 0;
}

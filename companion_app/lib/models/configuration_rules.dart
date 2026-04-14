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
        message: 'Brain Block is required but not found in configuration',
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
          message: 'Brain Block must be at position 0 (first block)',
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
              ? 'Button Press at position $i must be immediately preceded by an If Block'
              : 'Button Press at position $i must immediately follow an If Block (found ${prev.displayName} before it)',
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
              'Button Press at position $i must be immediately followed by a Then Block',
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
              'Button Press at position $i must be immediately followed by a Then Block (found ${next?.displayName ?? "unknown"} next)',
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
            'Multiple blocks use I²C address 0x${entry.key.toRadixString(16).toUpperCase().padLeft(2, "0")} at positions ${indices.join(", ")}',
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
            message: 'Then Block at position $i is not preceded by an If Block',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else if (stack.last.type == BlockType.loopBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.loopSequenceInvalidBlock,
            message:
                'Then Block at position $i cannot follow an open Loop Block at position ${stack.last.startIndex}; add an If Block (and Button Press) before Then',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else if (stack.last.type != BlockType.ifBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.unmatchedEnd,
            message: 'Then Block at position $i is not preceded by an If Block',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else if (stack.last.hasThen) {
          violations.add(RuleViolation(
            type: RuleViolationType.ifSequenceInvalidBlock,
            message: 'If Block at position ${stack.last.startIndex} already has a Then Block',
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
              message: 'If Block at position $ifStart must be immediately followed by Button Press before the Then Block',
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
            message: 'End If Block at position $i has no matching If Block',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else if (stack.last.type == BlockType.loopBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.sequenceInterleaved,
            message:
                'End If Block at position $i cannot close before the open Loop Block at position ${stack.last.startIndex}; use End Loop first',
            severity: Severity.warning,
            blockIndex: i,
          ));
        } else if (stack.last.type != BlockType.ifBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.unmatchedEnd,
            message: 'End If Block at position $i has no matching If Block',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else {
          final frame = stack.removeLast();
          if (!frame.hasThen) {
            violations.add(RuleViolation(
              type: RuleViolationType.ifSequenceIncomplete,
              message: 'If Block at position ${frame.startIndex} is missing a Then Block',
              severity: Severity.error,
              blockIndex: frame.startIndex,
            ));
          }
          if (!frame.hasContent(i)) {
            violations.add(RuleViolation(
              type: RuleViolationType.emptySequence,
              message: 'If Block sequence starting at ${frame.startIndex} has no executable content',
              severity: Severity.warning,
              blockIndex: frame.startIndex,
            ));
          }
        }
      } else if (type == BlockType.endLoopBlock) {
        if (stack.isEmpty) {
          violations.add(RuleViolation(
            type: RuleViolationType.unmatchedEnd,
            message: 'End Loop Block at position $i has no matching Loop Block',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else if (stack.last.type == BlockType.ifBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.sequenceInterleaved,
            message:
                'End Loop Block at position $i cannot close before the open If Block at position ${stack.last.startIndex}; use End If first',
            severity: Severity.warning,
            blockIndex: i,
          ));
        } else if (stack.last.type != BlockType.loopBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.unmatchedEnd,
            message: 'End Loop Block at position $i has no matching Loop Block',
            severity: Severity.error,
            blockIndex: i,
          ));
        } else {
          final frame = stack.removeLast();
          if (!frame.hasContent(i)) {
            violations.add(RuleViolation(
              type: RuleViolationType.emptySequence,
              message: 'Loop Block sequence starting at ${frame.startIndex} has no executable content',
              severity: Severity.warning,
              blockIndex: frame.startIndex,
            ));
          }
        }
      } else if (type?.isOutput ?? false || type == BlockType.delayBlock) {
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
        message: '${frame.type == BlockType.ifBlock ? "If" : "Loop"} Block at position ${frame.startIndex} is not properly terminated',
        severity: Severity.error,
        blockIndex: frame.startIndex,
      ));
    }

    return violations;
  }

  /// Validate all rules against a configuration
  static List<RuleViolation> validateAll(BlockConfiguration config) {
    final violations = <RuleViolation>[];

    // Check Brain Block rule
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

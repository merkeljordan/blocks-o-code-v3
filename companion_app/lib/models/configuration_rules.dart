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
  ifSequenceIncomplete,
  ifSequenceInvalidBlock,
  loopSequenceIncomplete,
  loopSequenceInvalidBlock,
  sequenceInterleaved,
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

  /// Validate If Block sequences
  static List<RuleViolation> checkIfBlockSequences(BlockConfiguration config) {
    final violations = <RuleViolation>[];
    final blocks = config.blocks;

    // Track state while scanning for If Block sequences
    int i = 0;
    while (i < blocks.length) {
      final block = blocks[i];

      // Start of If Block sequence
      if (block.blockType == BlockType.ifBlock) {
        final sequenceStart = i;
        final sequenceBlocks = <int>[i]; // Track indices in sequence

        // Expected sequence: If → Input → Then → Output(s) → End If
        i++; // Move to next block

        // Check for Input Block (Button Press)
        if (i >= blocks.length || blocks[i].blockType != BlockType.buttonPress) {
          violations.add(RuleViolation(
            type: RuleViolationType.ifSequenceInvalidBlock,
            message: 'If Block at position $sequenceStart must be followed by an Input Block (Button Press)',
            severity: Severity.error,
            blockIndex: sequenceStart,
            expectedBlockType: BlockType.buttonPress,
            actualBlockType: i < blocks.length ? blocks[i].blockType : null,
            affectedBlockIndices: [sequenceStart, i < blocks.length ? i : -1],
          ));
          // Try to continue from next block
          if (i < blocks.length) i++;
          continue;
        }
        sequenceBlocks.add(i);
        i++;

        // Check for Then Block
        if (i >= blocks.length || blocks[i].blockType != BlockType.thenBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.ifSequenceInvalidBlock,
            message: 'If Block sequence must have a Then Block after the Input Block',
            severity: Severity.error,
            blockIndex: sequenceStart,
            expectedBlockType: BlockType.thenBlock,
            actualBlockType: i < blocks.length ? blocks[i].blockType : null,
            affectedBlockIndices: sequenceBlocks + [i < blocks.length ? i : -1],
          ));
          if (i < blocks.length) i++;
          continue;
        }
        sequenceBlocks.add(i);
        i++;

        // Check for at least one Output Block or Delay Block
        bool hasOutput = false;
        while (i < blocks.length) {
          final currentBlock = blocks[i];
          if (currentBlock.blockType == BlockType.endIfBlock) {
            // Found End If Block - sequence complete
            sequenceBlocks.add(i);
            break;
          } else if (currentBlock.blockType?.isOutput ?? false) {
            // Valid output block
            hasOutput = true;
            sequenceBlocks.add(i);
            i++;
          } else if (currentBlock.blockType == BlockType.delayBlock) {
            // Valid delay block
            hasOutput = true;
            sequenceBlocks.add(i);
            i++;
          } else {
            // Invalid block in sequence
            violations.add(RuleViolation(
              type: RuleViolationType.ifSequenceInvalidBlock,
              message: 'If Block sequence contains invalid block type: ${currentBlock.blockType?.displayName ?? "Unknown"} at position $i',
              severity: Severity.error,
              blockIndex: i,
              expectedBlockType: BlockType.endIfBlock, // Expected End If or another output
              actualBlockType: currentBlock.blockType,
              affectedBlockIndices: sequenceBlocks + [i],
            ));
            i++;
            break;
          }
        }

        // Check if sequence was properly terminated
        if (i >= blocks.length || blocks[i].blockType != BlockType.endIfBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.ifSequenceIncomplete,
            message: 'If Block sequence starting at position $sequenceStart is not properly terminated with End If Block',
            severity: Severity.error,
            blockIndex: sequenceStart,
            expectedBlockType: BlockType.endIfBlock,
            affectedBlockIndices: sequenceBlocks,
          ));
        } else if (!hasOutput) {
          violations.add(RuleViolation(
            type: RuleViolationType.ifSequenceIncomplete,
            message: 'If Block sequence starting at position $sequenceStart must have at least one Output Block',
            severity: Severity.error,
            blockIndex: sequenceStart,
            affectedBlockIndices: sequenceBlocks,
          ));
        }

        i++; // Move past End If Block
      } else {
        i++; // Continue scanning
      }
    }

    return violations;
  }

  /// Validate Loop Block sequences
  static List<RuleViolation> checkLoopBlockSequences(BlockConfiguration config) {
    final violations = <RuleViolation>[];
    final blocks = config.blocks;

    int i = 0;
    while (i < blocks.length) {
      final block = blocks[i];

      // Start of Loop Block sequence
      if (block.blockType == BlockType.loopBlock) {
        final sequenceStart = i;
        final sequenceBlocks = <int>[i];
        i++; // Move to next block

        // Check for at least one Output Block or Delay Block
        bool hasOutput = false;
        while (i < blocks.length) {
          final currentBlock = blocks[i];
          if (currentBlock.blockType == BlockType.endLoopBlock) {
            // Found End Loop Block - sequence complete
            sequenceBlocks.add(i);
            break;
          } else if (currentBlock.blockType?.isOutput ?? false) {
            // Valid output block
            hasOutput = true;
            sequenceBlocks.add(i);
            i++;
          } else if (currentBlock.blockType == BlockType.delayBlock) {
            // Valid delay block
            hasOutput = true;
            sequenceBlocks.add(i);
            i++;
          } else {
            // Invalid block in sequence
            violations.add(RuleViolation(
              type: RuleViolationType.loopSequenceInvalidBlock,
              message: 'Loop Block sequence contains invalid block type: ${currentBlock.blockType?.displayName ?? "Unknown"} at position $i',
              severity: Severity.error,
              blockIndex: i,
              expectedBlockType: BlockType.endLoopBlock, // Expected End Loop or another output
              actualBlockType: currentBlock.blockType,
              affectedBlockIndices: sequenceBlocks + [i],
            ));
            i++;
            break;
          }
        }

        // Check if sequence was properly terminated
        if (i >= blocks.length || blocks[i].blockType != BlockType.endLoopBlock) {
          violations.add(RuleViolation(
            type: RuleViolationType.loopSequenceIncomplete,
            message: 'Loop Block sequence starting at position $sequenceStart is not properly terminated with End Loop Block',
            severity: Severity.error,
            blockIndex: sequenceStart,
            expectedBlockType: BlockType.endLoopBlock,
            affectedBlockIndices: sequenceBlocks,
          ));
        } else if (!hasOutput) {
          violations.add(RuleViolation(
            type: RuleViolationType.loopSequenceIncomplete,
            message: 'Loop Block sequence starting at position $sequenceStart must have at least one Output Block',
            severity: Severity.error,
            blockIndex: sequenceStart,
            affectedBlockIndices: sequenceBlocks,
          ));
        }

        i++; // Move past End Loop Block
      } else {
        i++; // Continue scanning
      }
    }

    return violations;
  }

  /// Check for interleaved sequences (warning)
  static List<RuleViolation> checkSequenceIsolation(BlockConfiguration config) {
    final violations = <RuleViolation>[];
    final blocks = config.blocks;

    // Track active sequences
    bool inIfSequence = false;
    bool inLoopSequence = false;

    for (int i = 0; i < blocks.length; i++) {
      final block = blocks[i];
      final blockType = block.blockType;

      if (blockType == BlockType.ifBlock) {
        if (inLoopSequence) {
          violations.add(RuleViolation(
            type: RuleViolationType.sequenceInterleaved,
            message: 'If Block at position $i starts while a Loop Block sequence is still active',
            severity: Severity.warning,
            blockIndex: i,
            affectedBlockIndices: [i],
          ));
        }
        inIfSequence = true;
      } else if (blockType == BlockType.endIfBlock) {
        inIfSequence = false;
      } else if (blockType == BlockType.loopBlock) {
        if (inIfSequence) {
          violations.add(RuleViolation(
            type: RuleViolationType.sequenceInterleaved,
            message: 'Loop Block at position $i starts while an If Block sequence is still active',
            severity: Severity.warning,
            blockIndex: i,
            affectedBlockIndices: [i],
          ));
        }
        inLoopSequence = true;
      } else if (blockType == BlockType.endLoopBlock) {
        inLoopSequence = false;
      }
    }

    return violations;
  }

  /// Validate all rules against a configuration
  static List<RuleViolation> validateAll(BlockConfiguration config) {
    final violations = <RuleViolation>[];

    // Check Brain Block rule
    violations.addAll(checkBrainBlockRule(config));

    // Always validate sequences too, so users can see all placement issues
    // in a single pass (Brain Block plus If/Loop/ordering problems).
    violations.addAll(checkIfBlockSequences(config));
    violations.addAll(checkLoopBlockSequences(config));
    violations.addAll(checkSequenceIsolation(config));

    return violations;
  }
}

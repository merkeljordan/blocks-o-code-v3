import 'package:demo_app/models/block_configuration.dart';
import 'package:demo_app/models/block_type.dart';
import 'package:demo_app/models/configuration_rules.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('ConfigurationRules button + if placement', () {
    test('accepts canonical If immediately followed by Button Press', () {
      final config = _config([
        BlockType.brainBlock,
        BlockType.ifBlock,
        BlockType.buttonPress,
        BlockType.thenBlock,
        BlockType.noteBlock,
        BlockType.endIfBlock,
      ]);
      final v = ConfigurationRules.validateAll(config);
      expect(
        v.where((e) => e.type == RuleViolationType.buttonPressMustFollowIf),
        isEmpty,
      );
      expect(
        v.where((e) => e.type == RuleViolationType.ifSequenceInvalidBlock),
        isEmpty,
      );
    });

    test('rejects Button Press not immediately after If', () {
      final config = _config([
        BlockType.brainBlock,
        BlockType.ifBlock,
        BlockType.delayBlock,
        BlockType.buttonPress,
        BlockType.thenBlock,
        BlockType.noteBlock,
        BlockType.endIfBlock,
      ]);
      final v = ConfigurationRules.validateAll(config);
      expect(
        v.any((e) => e.type == RuleViolationType.buttonPressMustFollowIf),
        isTrue,
      );
      expect(
        v.any((e) => e.type == RuleViolationType.ifSequenceInvalidBlock),
        isTrue,
      );
    });

    test('rejects Button Press after Loop (no If in front)', () {
      final config = _config([
        BlockType.brainBlock,
        BlockType.loopBlock,
        BlockType.buttonPress,
        BlockType.endLoopBlock,
      ]);
      final v = ConfigurationRules.validateAll(config);
      expect(
        v.any((e) => e.type == RuleViolationType.buttonPressMustFollowIf),
        isTrue,
      );
    });
  });

  group('Then immediately after Button (rule 3)', () {
    test('rejects Delay between Button Press and Then', () {
      final config = _config([
        BlockType.brainBlock,
        BlockType.ifBlock,
        BlockType.buttonPress,
        BlockType.delayBlock,
        BlockType.thenBlock,
        BlockType.noteBlock,
        BlockType.endIfBlock,
      ]);
      final v = ConfigurationRules.validateAll(config);
      expect(
        v.any((e) => e.type == RuleViolationType.thenMustImmediatelyFollowButton),
        isTrue,
      );
    });
  });

  group('Sequence interleaved (rule 4)', () {
    test('warns when End If appears before inner Loop is closed', () {
      final config = _config([
        BlockType.brainBlock,
        BlockType.ifBlock,
        BlockType.loopBlock,
        BlockType.endIfBlock,
      ]);
      final v = ConfigurationRules.validateAll(config);
      expect(
        v.any((e) => e.type == RuleViolationType.sequenceInterleaved),
        isTrue,
      );
      expect(
        v
            .where((e) => e.type == RuleViolationType.sequenceInterleaved)
            .every((e) => e.severity == Severity.warning),
        isTrue,
      );
    });

    test('warns when End Loop appears before inner If is closed', () {
      final config = _config([
        BlockType.brainBlock,
        BlockType.loopBlock,
        BlockType.ifBlock,
        BlockType.buttonPress,
        BlockType.thenBlock,
        BlockType.endLoopBlock,
      ]);
      final v = ConfigurationRules.validateAll(config);
      expect(
        v.any((e) => e.type == RuleViolationType.sequenceInterleaved),
        isTrue,
      );
    });
  });

  group('Loop sequence invalid (rule 5)', () {
    test('rejects Then immediately after Loop', () {
      final config = _config([
        BlockType.brainBlock,
        BlockType.loopBlock,
        BlockType.thenBlock,
        BlockType.endLoopBlock,
      ]);
      final v = ConfigurationRules.validateAll(config);
      expect(
        v.any((e) => e.type == RuleViolationType.loopSequenceInvalidBlock),
        isTrue,
      );
    });
  });

  group('Duplicate I2C (rule 6)', () {
    test('rejects two blocks with the same I2C address', () {
      final config = _configWithI2c([
        BlockType.brainBlock,
        BlockType.noteBlock,
        BlockType.musicSequenceBlock,
      ], [
        8,
        10,
        10,
      ]);
      final v = ConfigurationRules.validateAll(config);
      expect(
        v.any((e) => e.type == RuleViolationType.duplicateI2cAddress),
        isTrue,
      );
      final dup =
          v.firstWhere((e) => e.type == RuleViolationType.duplicateI2cAddress);
      expect(dup.affectedBlockIndices, containsAll([1, 2]));
    });
  });
}

BlockConfiguration _config(List<BlockType> types) {
  return _configWithI2c(types, null);
}

BlockConfiguration _configWithI2c(List<BlockType> types, List<int>? i2c) {
  final blocks = <BlockInfo>[];
  for (var i = 0; i < types.length; i++) {
    final type = types[i];
    final addr = i2c != null ? i2c[i] : i;
    blocks.add(
      BlockInfo(
        index: i,
        i2cAddress: addr,
        connectionOrder: i,
        blockType: type,
        whoami: WhoAmIData(
          blockType: type.identifier,
          blockId: 'BLOCK_${i.toString().padLeft(2, '0')}',
          firmwareVersion: '1.0.0',
          capabilities: const [],
        ),
      ),
    );
  }
  return BlockConfiguration(totalBlocks: blocks.length, blocks: blocks);
}

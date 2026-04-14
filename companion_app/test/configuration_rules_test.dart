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
}

BlockConfiguration _config(List<BlockType> types) {
  final blocks = <BlockInfo>[];
  for (var i = 0; i < types.length; i++) {
    final type = types[i];
    blocks.add(
      BlockInfo(
        index: i,
        i2cAddress: i,
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

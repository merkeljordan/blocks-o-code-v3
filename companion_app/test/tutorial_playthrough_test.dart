import 'dart:io';

import 'package:demo_app/main.dart';
import 'package:demo_app/models/block_configuration.dart';
import 'package:demo_app/models/block_type.dart';
import 'package:demo_app/widgets/block_3d_visualizer.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('Tutorial playthrough MVP', () {
    testWidgets('shows home prompt and allows skip', (tester) async {
      await tester.binding.setSurfaceSize(const Size(1400, 1000));
      await tester.pumpWidget(const MaterialApp(home: MainScreen()));
      await tester.pump(const Duration(milliseconds: 300));

      expect(find.text('Try the guided playthrough'), findsOneWidget);
      expect(find.byKey(const Key('playthrough_start_button')), findsOneWidget);
      expect(find.byKey(const Key('playthrough_skip_button')), findsOneWidget);

      await tester.tap(find.byKey(const Key('playthrough_skip_button')));
      await tester.pump(const Duration(milliseconds: 300));

      expect(find.text('Try the guided playthrough'), findsNothing);
      await tester.binding.setSurfaceSize(null);
    });

    testWidgets('start opens overlay and can restart from beginning', (
      tester,
    ) async {
      await tester.binding.setSurfaceSize(const Size(1400, 1000));
      await tester.pumpWidget(const MaterialApp(home: MainScreen()));
      await tester.pump(const Duration(milliseconds: 300));

      await tester.tap(find.byKey(const Key('playthrough_start_button')));
      await tester.pump(const Duration(milliseconds: 300));

      expect(
        find.byKey(const Key('playthrough_progress_panel')),
        findsOneWidget,
      );
      expect(find.text('Open Block Workspace'), findsOneWidget);
      expect(
        find.byKey(const Key('playthrough_go_workspace_button')),
        findsOneWidget,
      );

      final dynamic state = tester.state(find.byType(MainScreen));
      state.debugApplyConfigurationForTest(
        _buildConfig([BlockType.brainBlock]),
      );
      await _pumpTutorialStepTransition(tester);
      expect(find.text('Step 1: Add If Block'), findsOneWidget);

      await tester.tap(find.byKey(const Key('playthrough_restart_button')));
      await tester.pump(const Duration(milliseconds: 300));
      expect(find.text('Open Block Workspace'), findsOneWidget);
      await tester.binding.setSurfaceSize(null);
    });

    testWidgets('advances through scenario and completes', (tester) async {
      await tester.binding.setSurfaceSize(const Size(1400, 1000));
      await tester.pumpWidget(const MaterialApp(home: MainScreen()));
      await tester.pump(const Duration(milliseconds: 300));

      await tester.tap(find.byKey(const Key('playthrough_start_button')));
      await tester.pump(const Duration(milliseconds: 300));

      final dynamic state = tester.state(find.byType(MainScreen));
      state.debugApplyConfigurationForTest(
        _buildConfig([BlockType.brainBlock]),
      );
      await _pumpTutorialStepTransition(tester);
      expect(find.text('Step 1: Add If Block'), findsOneWidget);

      state.debugApplyConfigurationForTest(
        _buildConfig([BlockType.brainBlock, BlockType.ifBlock]),
      );
      await _pumpTutorialStepTransition(tester);
      expect(find.text('Step 2: Add Button Press'), findsOneWidget);

      state.debugApplyConfigurationForTest(
        _buildConfig([
          BlockType.brainBlock,
          BlockType.ifBlock,
          BlockType.buttonPress,
        ]),
      );
      await _pumpTutorialStepTransition(tester);
      expect(find.text('Step 3: Add Then Block'), findsOneWidget);

      state.debugApplyConfigurationForTest(
        _buildConfig([
          BlockType.brainBlock,
          BlockType.ifBlock,
          BlockType.buttonPress,
          BlockType.thenBlock,
        ]),
      );
      await _pumpTutorialStepTransition(tester);
      expect(find.text('Step 4: Add Note Block'), findsOneWidget);
      if (Platform.isWindows) {
        expect(find.byType(Block3DVisualizer), findsOneWidget);
      }

      state.debugApplyConfigurationForTest(
        _buildConfig([
          BlockType.brainBlock,
          BlockType.ifBlock,
          BlockType.buttonPress,
          BlockType.thenBlock,
          BlockType.noteBlock,
        ]),
      );
      await _pumpTutorialStepTransition(tester);
      expect(find.text('Step 5: Add End If Block'), findsOneWidget);

      state.debugApplyConfigurationForTest(
        _buildConfig([
          BlockType.brainBlock,
          BlockType.ifBlock,
          BlockType.buttonPress,
          BlockType.thenBlock,
          BlockType.noteBlock,
          BlockType.endIfBlock,
        ]),
      );
      await _pumpTutorialStepTransition(tester);

      expect(find.text('Playthrough complete'), findsOneWidget);
      expect(find.byKey(const Key('playthrough_progress_panel')), findsNothing);

      await tester.tap(find.text('Close'));
      await tester.pump(const Duration(milliseconds: 300));
      expect(find.text('Playthrough complete'), findsNothing);
      await tester.binding.setSurfaceSize(null);
    });
  });
}

BlockConfiguration _buildConfig(List<BlockType> types) {
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

Future<void> _pumpTutorialStepTransition(WidgetTester tester) async {
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 300));
}

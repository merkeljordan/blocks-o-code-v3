/// Enumeration of all supported block types in the system
enum BlockType {
  brainBlock,
  ifBlock,
  thenBlock,
  endIfBlock,
  discoModeBlock,
  noteBlock,
  musicSequenceBlock,
  ledColorFlashBlock,
  buttonPress,
  loopBlock,
  endLoopBlock,
  delayBlock;

  /// Get the string identifier for this block type (used in JSON/WHOAMI)
  String get identifier {
    switch (this) {
      case BlockType.brainBlock:
        return 'brain_block';
      case BlockType.ifBlock:
        return 'if_block';
      case BlockType.thenBlock:
        return 'then_block';
      case BlockType.endIfBlock:
        return 'end_if_block';
      case BlockType.discoModeBlock:
        return 'disco_mode_block';
      case BlockType.noteBlock:
        return 'note_block';
      case BlockType.musicSequenceBlock:
        return 'music_sequence_block';
      case BlockType.ledColorFlashBlock:
        return 'led_color_flash_block';
      case BlockType.buttonPress:
        return 'button_press';
      case BlockType.loopBlock:
        return 'loop_block';
      case BlockType.endLoopBlock:
        return 'end_loop_block';
      case BlockType.delayBlock:
        return 'delay_block';
    }
  }

  /// Get the display name for this block type
  String get displayName {
    switch (this) {
      case BlockType.brainBlock:
        return 'Brain Block';
      case BlockType.ifBlock:
        return 'If Block';
      case BlockType.thenBlock:
        return 'Then Block';
      case BlockType.endIfBlock:
        return 'End If Block';
      case BlockType.discoModeBlock:
        return 'Disco Mode Block';
      case BlockType.noteBlock:
        return 'Note Block';
      case BlockType.musicSequenceBlock:
        return 'Music Sequence Block';
      case BlockType.ledColorFlashBlock:
        return 'LED Color Flash Block';
      case BlockType.buttonPress:
        return 'Button Press';
      case BlockType.loopBlock:
        return 'Loop Block';
      case BlockType.endLoopBlock:
        return 'End Loop Block';
      case BlockType.delayBlock:
        return 'Delay Block';
    }
  }

  /// Get the category of this block type
  BlockCategory get category {
    switch (this) {
      case BlockType.brainBlock:
        return BlockCategory.controlSystem;
      case BlockType.ifBlock:
      case BlockType.thenBlock:
      case BlockType.endIfBlock:
      case BlockType.loopBlock:
      case BlockType.endLoopBlock:
      case BlockType.delayBlock:
        return BlockCategory.controlFlow;
      case BlockType.buttonPress:
        return BlockCategory.input;
      case BlockType.discoModeBlock:
      case BlockType.noteBlock:
      case BlockType.musicSequenceBlock:
      case BlockType.ledColorFlashBlock:
        return BlockCategory.output;
    }
  }

  /// Parse a block type from a string identifier
  static BlockType? fromIdentifier(String identifier) {
    for (var type in BlockType.values) {
      if (type.identifier == identifier) {
        return type;
      }
    }
    return null;
  }

  /// Check if this is a control flow block
  bool get isControlFlow => category == BlockCategory.controlFlow;

  /// Check if this is an input block
  bool get isInput => category == BlockCategory.input;

  /// Check if this is an output block
  bool get isOutput => category == BlockCategory.output;

  /// Check if this is the brain block
  bool get isBrainBlock => this == BlockType.brainBlock;

  /// Check if this is a sequence terminator (End If Block or End Loop Block)
  bool get isTerminator => this == BlockType.endIfBlock || this == BlockType.endLoopBlock;
}

/// Categories for block types
enum BlockCategory {
  controlSystem,
  controlFlow,
  input,
  output;
}

/// Metadata definition for a block type
class BlockTypeDefinition {
  final BlockType type;
  final String displayName;
  final BlockCategory category;
  final List<String> capabilities;
  final String iconName; // Icon identifier for UI

  const BlockTypeDefinition({
    required this.type,
    required this.displayName,
    required this.category,
    this.capabilities = const [],
    this.iconName = 'block',
  });

  /// Get all block type definitions
  static List<BlockTypeDefinition> getAll() {
    return [
      BlockTypeDefinition(
        type: BlockType.brainBlock,
        displayName: 'Brain Block',
        category: BlockCategory.controlSystem,
        capabilities: ['system_control', 'i2c_master'],
        iconName: 'brain',
      ),
      BlockTypeDefinition(
        type: BlockType.ifBlock,
        displayName: 'If Block',
        category: BlockCategory.controlFlow,
        capabilities: ['conditional_logic'],
        iconName: 'if',
      ),
      BlockTypeDefinition(
        type: BlockType.thenBlock,
        displayName: 'Then Block',
        category: BlockCategory.controlFlow,
        capabilities: ['conditional_logic'],
        iconName: 'then',
      ),
      BlockTypeDefinition(
        type: BlockType.endIfBlock,
        displayName: 'End If Block',
        category: BlockCategory.controlFlow,
        capabilities: ['conditional_terminator'],
        iconName: 'end_if',
      ),
      BlockTypeDefinition(
        type: BlockType.discoModeBlock,
        displayName: 'Disco Mode Block',
        category: BlockCategory.output,
        capabilities: ['led_pattern', 'visual_output'],
        iconName: 'disco',
      ),
      BlockTypeDefinition(
        type: BlockType.noteBlock,
        displayName: 'Note Block',
        category: BlockCategory.output,
        capabilities: ['audio_output', 'tone_generation'],
        iconName: 'note',
      ),
      BlockTypeDefinition(
        type: BlockType.musicSequenceBlock,
        displayName: 'Music Sequence Block',
        category: BlockCategory.output,
        capabilities: ['audio_output', 'sequence_playback'],
        iconName: 'music',
      ),
      BlockTypeDefinition(
        type: BlockType.ledColorFlashBlock,
        displayName: 'LED Color Flash Block',
        category: BlockCategory.output,
        capabilities: ['led_pattern', 'color_output'],
        iconName: 'led_flash',
      ),
      BlockTypeDefinition(
        type: BlockType.buttonPress,
        displayName: 'Button Press',
        category: BlockCategory.input,
        capabilities: ['button_input', 'user_interaction'],
        iconName: 'button',
      ),
      BlockTypeDefinition(
        type: BlockType.loopBlock,
        displayName: 'Loop Block',
        category: BlockCategory.controlFlow,
        capabilities: ['iteration', 'repetition'],
        iconName: 'loop',
      ),
      BlockTypeDefinition(
        type: BlockType.endLoopBlock,
        displayName: 'End Loop Block',
        category: BlockCategory.controlFlow,
        capabilities: ['iteration_terminator'],
        iconName: 'end_loop',
      ),
      BlockTypeDefinition(
        type: BlockType.delayBlock,
        displayName: 'Delay Block',
        category: BlockCategory.controlFlow,
        capabilities: ['timing_control', 'execution_pause'],
        iconName: 'delay',
      ),
    ];
  }

  /// Get definition for a specific block type
  static BlockTypeDefinition? getDefinition(BlockType type) {
    return getAll().firstWhere(
      (def) => def.type == type,
      orElse: () => throw StateError('No definition found for $type'),
    );
  }
}

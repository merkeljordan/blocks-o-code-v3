import '../models/block_configuration.dart';
import '../models/configuration_rules.dart';

/// Service for validating block configurations against rules
class ConfigurationValidator {
  /// Validate a configuration and return all rule violations
  List<RuleViolation> validate(BlockConfiguration config) {
    return ConfigurationRules.validateAll(config);
  }

  /// Get only error-level violations
  List<RuleViolation> getErrors(BlockConfiguration config) {
    return validate(config)
        .where((v) => v.severity == Severity.error)
        .toList();
  }

  /// Get only warning-level violations
  List<RuleViolation> getWarnings(BlockConfiguration config) {
    return validate(config)
        .where((v) => v.severity == Severity.warning)
        .toList();
  }

  /// Check if configuration is valid (no errors)
  bool isValid(BlockConfiguration config) {
    return getErrors(config).isEmpty;
  }

  /// Get a summary of validation results
  ValidationSummary getSummary(BlockConfiguration config) {
    final violations = validate(config);
    final errors = violations.where((v) => v.severity == Severity.error).toList();
    final warnings = violations.where((v) => v.severity == Severity.warning).toList();

    return ValidationSummary(
      isValid: errors.isEmpty,
      totalViolations: violations.length,
      errorCount: errors.length,
      warningCount: warnings.length,
      violations: violations,
    );
  }
}

/// Summary of validation results
class ValidationSummary {
  final bool isValid;
  final int totalViolations;
  final int errorCount;
  final int warningCount;
  final List<RuleViolation> violations;

  ValidationSummary({
    required this.isValid,
    required this.totalViolations,
    required this.errorCount,
    required this.warningCount,
    required this.violations,
  });

  @override
  String toString() {
    return 'ValidationSummary(isValid: $isValid, errors: $errorCount, warnings: $warningCount)';
  }
}

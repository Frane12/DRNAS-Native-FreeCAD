#pragma once
#include <QString>

// Android-native diagnostic logging. Current and previous session logs are
// stored under private QStandardPaths::AppDataLocation/diagnostics.
namespace DrnasDiagnostics {
void start();
void note(const QString& message);
QString report();
}

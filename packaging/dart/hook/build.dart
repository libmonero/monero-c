// The build hook of monero_native. It gives the build of a Dart or Flutter program the library of monero-c for the target. The library is in
// the archive of the release of the same version on GitHub, and the hook checks the SHA-256 of the archive before it extracts it.
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:archive/archive.dart';
import 'package:code_assets/code_assets.dart';
import 'package:crypto/crypto.dart';
import 'package:hooks/hooks.dart';

/// The name of the code asset of the library, so its id is `package:monero_native/monero_c.dart`.
const _assetName = 'monero_c.dart';

const _defaultReleaseUrl = 'https://github.com/libmonero/monero-c/releases/download';

void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;

    final code = input.config.code;
    final target = _Target.of(code.targetOS, code.targetArchitecture);
    final pubspec = input.packageRoot.resolve('pubspec.yaml');
    output.dependencies.add(pubspec);

    final directory = await _files(input, target, _version(pubspec));
    output.assets.code.add(
      CodeAsset(
        package: input.packageName,
        name: _assetName,
        linkMode: DynamicLoadingBundled(),
        file: directory.uri.resolve(target.library),
      ),
    );
    // the library finds these next to itself, so they go to the same folder of the bundle
    for (final file in target.companions) {
      output.assets.code.add(
        CodeAsset(
          package: input.packageName,
          name: file,
          linkMode: DynamicLoadingBundled(),
          file: directory.uri.resolve(file),
        ),
      );
    }
  });
}

/// A platform that has a library, as the archives of the release call it.
class _Target {
  const _Target(this.platform, this.library, [this.companions = const []]);

  final String platform;
  final String library;
  final List<String> companions;

  List<String> get files => [library, ...companions];

  String get archiveExtension => platform == 'windows' ? 'zip' : 'tar.gz';

  static _Target of(OS os, Architecture architecture) {
    if (os == OS.linux && architecture == Architecture.x64) {
      return const _Target('linux-x86_64', 'libmonero_c.so', ['libusb-1.0.so.0']);
    }
    if (os == OS.linux && architecture == Architecture.arm64) {
      return const _Target('linux-arm64', 'libmonero_c.so', ['libusb-1.0.so.0']);
    }
    if (os == OS.macOS && architecture == Architecture.x64) {
      return const _Target('mac-x86_64', 'libmonero_c.dylib', ['libusb-1.0.0.dylib']);
    }
    if (os == OS.macOS && architecture == Architecture.arm64) {
      return const _Target('mac-arm64', 'libmonero_c.dylib', ['libusb-1.0.0.dylib']);
    }
    // the Windows and Android libraries have no Trezor support, so they import no libusb
    if (os == OS.windows && architecture == Architecture.x64) {
      return const _Target('windows', 'libmonero_c.dll');
    }
    if (os == OS.android && architecture == Architecture.arm64) {
      return const _Target('android-arm64', 'libmonero_c.so');
    }
    if (os == OS.android && architecture == Architecture.x64) {
      return const _Target('android-x86_64', 'libmonero_c.so');
    }
    throw UnsupportedError('monero_native has no library for $os on $architecture');
  }
}

String _version(Uri pubspec) {
  final match = RegExp(r'^version:\s*(\S+)', multiLine: true).firstMatch(File.fromUri(pubspec).readAsStringSync());
  if (match == null) throw StateError('no version in $pubspec');
  return match.group(1)!;
}

/// The folder that has the files of the target: the one of the user define `library_dir`, or the extracted archive of the release.
Future<Directory> _files(BuildInput input, _Target target, String version) async {
  final local = input.userDefines.path('library_dir');
  if (local != null) {
    final directory = Directory.fromUri(local);
    _requireFiles(directory, target);
    return directory;
  }

  final directory = Directory.fromUri(input.outputDirectoryShared.resolve('monero-c-$version-${target.platform}/'));
  final complete = File.fromUri(directory.uri.resolve('.complete'));
  if (complete.existsSync()) return directory;

  final base = ((input.userDefines['release_url'] as String?) ?? _defaultReleaseUrl).replaceFirst(RegExp(r'/+$'), '');
  final release = '$base/v$version';
  final name = 'monero-c-$version-${target.platform}.${target.archiveExtension}';

  final bytes = await _get(Uri.parse('$release/$name'));
  final expected = await _checksum(input, release, name);
  final actual = sha256.convert(bytes).toString();
  if (actual != expected) {
    throw StateError('The SHA-256 of $name is $actual, and it has to be $expected.');
  }

  directory.createSync(recursive: true);
  _extract(bytes, target, directory);
  _requireFiles(directory, target);
  complete.writeAsStringSync(name);
  return directory;
}

void _requireFiles(Directory directory, _Target target) {
  for (final file in target.files) {
    if (!File.fromUri(directory.uri.resolve(file)).existsSync()) {
      throw StateError('${directory.path} has no $file.');
    }
  }
}

/// The SHA-256 of an archive: from `checksums.txt` of the package if it has one, which the release adds to the package that it publishes,
/// and from `SHA256SUMS.txt` of the release otherwise.
Future<String> _checksum(BuildInput input, String release, String name) async {
  final bundled = File.fromUri(input.packageRoot.resolve('checksums.txt'));
  final text = bundled.existsSync() ? bundled.readAsStringSync() : utf8.decode(await _get(Uri.parse('$release/SHA256SUMS.txt')));
  for (final line in const LineSplitter().convert(text)) {
    final parts = line.trim().split(RegExp(r'\s+'));
    if (parts.length == 2 && parts[1].replaceFirst('*', '') == name) return parts[0].toLowerCase();
  }
  throw StateError('There is no checksum of $name.');
}

/// Extracts the files of the target from the archive, which has them in a folder.
void _extract(List<int> bytes, _Target target, Directory directory) {
  final Archive archive;
  if (target.archiveExtension == 'zip') {
    archive = ZipDecoder().decodeBytes(bytes);
  } else {
    archive = TarDecoder().decodeBytes(GZipDecoder().decodeBytes(bytes));
  }
  for (final entry in archive) {
    if (!entry.isFile) continue;
    final path = entry.name.split('/');
    // only the files that the target needs, which are directly in the folder of the archive
    if (path.length != 2 || !target.files.contains(path[1])) continue;
    File.fromUri(directory.uri.resolve(path[1])).writeAsBytesSync(entry.content as List<int>);
  }
}

Future<List<int>> _get(Uri uri) async {
  final client = HttpClient();
  try {
    final response = await (await client.getUrl(uri)).close();
    if (response.statusCode != HttpStatus.ok) {
      await response.drain<void>();
      throw HttpException('${response.statusCode} for $uri', uri: uri);
    }
    final builder = BytesBuilder(copy: false);
    await for (final chunk in response) {
      builder.add(chunk);
    }
    return builder.takeBytes();
  } finally {
    client.close();
  }
}

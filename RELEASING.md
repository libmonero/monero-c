# Releasing

A release is a tag `v<version>` on a commit of `main`. CI builds the libraries and the packages (the NuGet package `Monero.Native`, and the ones
for Android, the JVM, Dart, Linux and Apple) for that tag and collects them in a draft GitHub release. Publishing the draft pushes the NuGet
package to nuget.org and to the GitHub Packages registry of the organization. The other packages are files of the release for now.

## Versions

There are two versions, and a release changes both. The package version is also in the pubspec of the Dart package.

* **Package version.** `MoneroNativeVersion` in [`packaging/dotnet-core/Version.props`](packaging/dotnet-core/Version.props). It is the version of
  `Monero.Native` and of the release, and the tag has to be `v` plus this version. A version with a hyphen, such as `0.1.1-alpha.1`, is a
  prerelease.
* **Dart version.** `version` in [`packaging/dart/pubspec.yaml`](packaging/dart/pubspec.yaml), which has to be the package version. pub.dev
  wants the number in the file, so it is a second place to set. `packaging/dart/build.sh` fails when it differs, and so does the Dart workflow.
* **ABI version.** `MONERO_C_ABI_VERSION_MAJOR`, `MINOR` and `PATCH` in [`src/utils/monero_c_utils.h`](src/utils/monero_c_utils.h), which
  `monero_utils_get_abi_version()` returns. While the major version is 0, a change of the minor version can break callers. The patch version is
  for fixes that leave the ABI as it is. The numbers are pinned in `tests/c/unit/monero_c_abi_unit_tests.c` and in `MoneroUtilsTests.cs` of the
  C# tests, so a change of the ABI version is always deliberate.

So far the ABI version has been the package version without the prerelease suffix. A change of a function, a layout or an enum value
bumps the minor version until 1.0, and a fix that leaves them as they are bumps the patch version.

## Make a release

1. In a branch, set `MoneroNativeVersion` and `version` in `packaging/dart/pubspec.yaml`, set the ABI version in the three places above, and add a section `## <version> (<date>)` to
   [`CHANGELOG.md`](CHANGELOG.md). The release notes are that section, so write what a user of the library has to know.
2. Merge the branch to `main` and wait for Build and Tests to pass on the merge commit. The release refuses a commit whose Tests workflow
   has no successful run, because Tests doesn't run on tags.
3. Tag that commit on `libmonero/monero-c` and push the tag:

   ```sh
   git tag -a v0.1.1-alpha.1 -m "monero-c 0.1.1-alpha.1" <commit>
   git push <remote of libmonero/monero-c> v0.1.1-alpha.1
   ```

4. Build runs on the tag, and its job `Release` creates a **draft** GitHub release. The job fails when the tag and `MoneroNativeVersion`
   differ, when the commit is not on `main`, and when the changelog has no section for the version. The draft has:
   * `monero-c-<version>-<platform>.tar.gz` (`.zip` for Windows) for `linux-x86_64`, `linux-arm64`, `mac-x86_64`, `mac-arm64`, `windows`,
     `android-arm64` and `android-x86_64`, each with the library, libusb where it has one, the public headers and the licenses. The Dart
     package downloads these, so they have to stay in the release under these names,
   * the NuGet package `Monero.Native.<version>.nupkg`,
   * `monero-native-android-<version>.aar` and `monero-native-jvm-<version>.jar`, each with its `.pom`,
   * `monero_native-<version>.tar.gz`, the Dart package with the checksums of the archives above, ready for `dart pub publish`,
   * `MoneroC-<version>.xcframework.zip`, whose SHA-256 is the checksum of a Swift `binaryTarget`,
   * `monero-c_<version>_amd64.deb` and `monero-c_<version>_arm64.deb`,
   * `SHA256SUMS.txt`.

   Nothing is public yet. If the run fails, delete the draft and the tag, fix it and tag again.
5. Download the draft, check the checksums and try the package, then **publish** the release. Publishing runs `publish-nuget.yml`, which pushes
   the package of the release to nuget.org and to GitHub Packages. The two jobs are independent: one registry that refuses the package
   doesn't stop the other.
6. In the projects that use the package, such as monero-csharp, move the submodule to the tagged commit and set their
   `MoneroNativeVersion` to the new version.

## If something goes wrong

* **A version can't be pushed twice.** nuget.org lets you unlist a version but not delete it or push it again, so a package that is wrong
  needs a new version.
* **A package that stays unlisted.** nuget.org checks a new version before it lists it. If it is still unlisted after a few days, write to
  their support from the page of the package. The GitHub release, its files and the package on GitHub Packages don't depend on nuget.org.
* **A rerun of the workflow.** GitHub Packages skips a version that is already there, so rerunning the workflow after a failure of the nuget.org
  job is safe. nuget.org refuses the second push of a version, so rerun only the failed job.
* **A wrong tag.** Before the release is published, delete the tag (`git push <remote> :refs/tags/v<version>`) and the draft, and tag
  again. After it is published, don't move the tag: make a new version.

## Setup, once

nuget.org gets no stored key. The workflow asks for one, valid for an hour, with the OIDC token of GitHub (Trusted Publishing).

* On nuget.org, open your profile menu, *Trusted Publishing*, and add a policy:
  * *Policy owner*: the organization **Libmonero** (you have to be an active member; the policy applies to the packages that it owns).
  * *Repository owner*: `libmonero`, *Repository*: `monero-c`, *Workflow file*: `publish-nuget.yml` (the name only, no path).
  * *Environment*: `nuget`.
  * *Scopes*: push new packages and new versions, with the glob `Monero.*`, so that the first push of `Monero.Native` is allowed.
* In the repository settings, create the environment `nuget` (add required reviewers if a second person should approve each push), and add the
  secret `NUGET_USER`: the **profile name** of the nuget.org account of the policy, not its email.
* Nothing else: no API key is created.

GitHub Packages needs no setup: its job pushes with the token of the run (`packages: write`). The file name `publish-nuget.yml` is part of the
nuget.org policy, so renaming the file means updating the policy first.

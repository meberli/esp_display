# Tag a release safely: tools/tag_release.ps1 0.9.1
# Checks that version.txt IN THE COMMIT TO BE TAGGED (origin/<branch>) equals
# the intended version, that HEAD is pushed, then tags that commit and pushes the tag.
param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
git fetch -q origin
$commit = (git rev-parse "origin/$branch").Trim()
if ((git rev-parse HEAD).Trim() -ne $commit) { throw "HEAD is not pushed to origin/$branch" }
$v = ((git show "${commit}:version.txt") -join '').Trim()
if ($v -ne $Version) { throw "version.txt in $commit is '$v', expected '$Version' - not tagging" }
if (git ls-remote --tags origin "v$Version") { throw "tag v$Version already exists on origin" }
git tag "v$Version" $commit
git push origin "v$Version"
Write-Host "tagged v$Version on $commit"
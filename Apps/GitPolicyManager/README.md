# Git Policy Manager

A daemon that applies repository policies to the repositories of Git hosting provider organizations. It applies them at start
and then once a day, and reports the result through its status sensor.

## Configuration

The policy manager reads `GitPolicyManagerConfig.json` in its directory:

```json
{
	"Pretend": false,
	"WarnWithoutPolicies": true,
	"Configs":
	[
		{
			"Provider": "GitHub",
			"Authentication":
			{
				"App":
				{
					"AppID": "123456",
					"InstallationID": "78901234",
					"PrivateKeyFile": "GitHubApp.pem"
				}
			},
			"Organizations": ["Malterlib"],
			"Policies":
			[
				{
					"Name": "Public",
					"IncludeRepositoryType": "Public",
					"ExcludeRepositoryWildcards": ["*_rLFS"],
					"Permissions": {"Teams": {"coders": "write"}, "RemoveOther": true},
					"GenericRules":
					{
						"Linear history":
						{
							"Target": "Branch",
							"IncludeRefNames": ["~ALL"],
							"ExcludeRefNames": [],
							"BypassActors": [],
							"Rules": [{"Type": "LinearHistory"}],
							"Enforcement": "Active"
						}
					},
					"RemoveOther": {"BranchProtection": true}
				}
			]
		}
	]
}
```

- `Pretend` reports what applying the policies would change without changing anything.
- `WarnWithoutPolicies` warns about the repositories of the organizations that no policy matches.
- Each policy applies to the repositories its filters match, and a repository gets the first policy that matches it:
  `IncludeRepository`, `ExcludeRepository`, `IncludeRepositoryWildcards`, `ExcludeRepositoryWildcards`, the same four for
  `DefaultBranch`, and `IncludeRepositoryType` (`Private` or `Public`).
- A policy has the sections of `CRepositoryPolicy` in `Malterlib/Core/Build/Shared_RepositoryPolicy.MSettings`:
  `RepositorySettings`, `Permissions`, `BranchProtection`, `GenericRules`, `ActionsSettings` and `RemoveOther`.

### Authentication

`Authentication` logs in to the provider in one of two ways:

- `{"Token": "<token>"}`: a token GitHub issued to a user.
- `{"App": {...}}`: a GitHub App installation, which creates installation tokens as it needs them.
  - `AppID`: the ID of the App.
  - `InstallationID`: the installation of the App in the organization, or `Repository` (`owner/name`) to look up the
    installation from a repository it is installed on.
  - `PrivateKeyFile`: the path of the App's private key, the PEM file GitHub lets you download. A relative path is relative to
    the directory of the config file. Keep the file readable only by the user the policy manager runs as.
  - `PrivateKey`: the PEM text of the key, in place of `PrivateKeyFile`. Giving both is an error.

The App needs Administration write on the repositories to apply rulesets, branch protection and permissions.

### Removing what a policy doesn't list

Applying a policy creates and updates the branch protection rules and rulesets it lists, and leaves others alone.
`RemoveOther` also deletes the others:

- `"RemoveOther": {"BranchProtection": true}` deletes the branch protection rules whose patterns the policy doesn't list. With
  no `BranchProtection` section it deletes them all, which removes classic branch protection once rulesets replace it.
- `"RemoveOther": {"GenericRules": true}` deletes the repository rulesets whose names the policy doesn't list. Rulesets of the
  organization are never deleted.

Deleting comes last, after the branch protection rules and rulesets the policy lists are created and updated, so that what
replaces the deleted rules is in place first. When applying them fails, nothing is deleted. Nothing is deleted either while a
ruleset the policy lists is left unchanged as unmanageable, since it may not be what replaces them, and the policy reports an
error instead.

`Pretend` reports what would be deleted.

### Rulesets the policy can't fully express

A ruleset on GitHub can have settings that the policy has no way to express:

- Rules and rule parameters it doesn't model, such as `merge_queue`, `copilot_code_review` or the `required_reviewers` of a
  pull request rule, are kept unchanged when the policy updates the ruleset. The update reports them as kept.
- A bypass actor of a type it doesn't model, such as `EnterpriseOwner`, or a bypass mode it doesn't model, makes the ruleset
  unmanageable: the policy reports it and doesn't update it, since the update would remove that bypass.

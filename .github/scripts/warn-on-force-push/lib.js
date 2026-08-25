// Core logic for the "warn on history-rewriting force-push" workflow, split
// out from index.js so it can be unit tested without a live GitHub API.

// GitHub Apps report `type: "Bot"`, but this repo's PAT-backed bots
// (esphbot, bluetoothbot, esphomebot) report `type: "User"` and need an
// explicit login check too.
const BOT_LOGINS = new Set(['esphbot', 'bluetoothbot', 'esphomebot']);

const HUMAN_REVIEW_STATES = ['COMMENTED', 'CHANGES_REQUESTED', 'APPROVED', 'DISMISSED'];

function isBot(user) {
  return user?.type === 'Bot' || BOT_LOGINS.has(user?.login?.toLowerCase());
}

// GitHub logins are case-insensitive, so compare case-folded.
function isAuthor(user, prAuthorLogin) {
  return typeof user?.login === 'string' && user.login.toLowerCase() === prAuthorLogin.toLowerCase();
}

// GitHub's "suggest a change" comments embed a ```suggestion fenced block.
// Restricting inline-comment counting to these (rather than any inline
// comment) keeps the signal to actionable feedback -- but note this alone
// does not exclude the PR author: a self-suggestion would still match. The
// `isAuthor` check in hasHumanReview is what excludes the author.
function isCodeSuggestion(body) {
  return typeof body === 'string' && /```suggestion\b/.test(body);
}

// compareCommits 404s if either `before` or `after` can't be resolved. Only
// call it a rewrite if `after` resolves and `before` doesn't -- otherwise
// (e.g. a fork was deleted or made private) stay silent rather than risk a
// false positive.
async function isRewrite(github, owner, repo, before, after, core) {
  let comparison;
  try {
    ({ data: comparison } = await github.rest.repos.compareCommits({
      owner,
      repo,
      base: before,
      head: after,
    }));
  } catch (err) {
    if (err.status !== 404) {
      throw err;
    }
    try {
      await github.rest.repos.getCommit({ owner, repo, ref: after });
    } catch {
      core.info(`Could not resolve ${after} — skipping.`);
      return false;
    }
    core.info(`Previous tip ${before} is no longer reachable — history was rewritten.`);
    return true;
  }
  // Fast-forward: `after` descends from `before` (status "ahead", behind_by
  // 0). Anything else -- diverged, or `before` ahead of `after` -- is a
  // rewrite.
  return comparison.status === 'diverged' || comparison.behind_by > 0;
}

// "Review has started" means someone other than the PR author (and not a
// bot) left an inline code-change suggestion, or submitted a top-level
// review with a countable state. GitHub lets the PR author submit a
// COMMENTED review on their own PR (only APPROVED/CHANGES_REQUESTED are
// blocked), so excluding the author has to apply to both checks, not just
// inline comments.
function hasHumanReview(reviewComments, reviews, prAuthorLogin) {
  const countsFor = user => !isBot(user) && !isAuthor(user, prAuthorLogin);
  return (
    reviewComments.some(c => countsFor(c.user) && isCodeSuggestion(c.body)) ||
    reviews.some(r => countsFor(r.user) && HUMAN_REVIEW_STATES.includes(r.state))
  );
}

// Git ref names allow backticks, which would break out of the inline code
// span this is always used inside. GitHub does not parse @-mentions or other
// markdown inside a code span (inline or fenced), so no further escaping is
// needed -- and a zero-width space would just be an invisible character a
// user copies along with the branch name.
function sanitizeForProse(branch) {
  return branch.replace(/`/g, '');
}

// `branch` (unsanitized) is used only inside the fenced restore command:
// GitHub renders neither @-mentions nor inline markdown inside a fenced code
// block, and the command must stay copy-pasteable as-is.
function buildWarningBody({ marker, branch, before, after, owner, repo }) {
  const safeBranch = sanitizeForProse(branch);
  return [
    marker,
    "### ⚠️ This branch's history was rewritten",
    '',
    `A force-push to \`${safeBranch}\` replaced earlier commits — the previous tip is no longer an ancestor of the current tip (\`${after.slice(0, 7)}\`).`,
    '',
    'Rewriting history mid-review makes it harder to track what changed since the last look, and can detach existing review comments from their code. Please avoid `git push --force` on PR branches — push new commits on top instead, or check with reviewers first if a rebase is really needed.',
    '',
    "**To restore the previous state, if this wasn't intentional:**",
    '',
    'From a local clone that still has the old commit, push the previous tip back onto the branch:',
    '```',
    `git push --force origin ${before}:${branch}`,
    '```',
    `Or view it first at https://github.com/${owner}/${repo}/commit/${before}.`,
    '',
    "That commit may no longer be fetchable with `git fetch` once no branch or tag references it, but it's often still viewable at the link above for a while.",
  ].join('\n');
}

module.exports = {
  isBot,
  isRewrite,
  hasHumanReview,
  sanitizeForProse,
  buildWarningBody,
};

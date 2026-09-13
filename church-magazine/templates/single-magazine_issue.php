<?php
/**
 * Default reading-view template for a single Magazine Issue.
 * A theme can override this by adding its own single-magazine_issue.php.
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

get_header();
?>

<div id="primary" class="content-area cm-issue-page">
	<main id="main" class="site-main">
		<?php CM_Render::render_issue( get_the_ID() ); ?>
	</main>
</div>

<?php
get_footer();

<?php
/**
 * Default archive template listing all Magazine Issues.
 * A theme can override this by adding its own archive-magazine_issue.php.
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

get_header();
?>

<div id="primary" class="content-area cm-archive-page">
	<main id="main" class="site-main">
		<h1 class="cm-archive-title"><?php esc_html_e( 'Magazine Issues', 'church-magazine' ); ?></h1>

		<?php if ( have_posts() ) : ?>
			<div class="cm-issue-grid">
				<?php
				while ( have_posts() ) :
					the_post();
					?>
					<a class="cm-issue-card" href="<?php the_permalink(); ?>">
						<?php if ( has_post_thumbnail() ) : ?>
							<div class="cm-issue-card-cover"><?php the_post_thumbnail( 'medium' ); ?></div>
						<?php endif; ?>
						<h2 class="cm-issue-card-title"><?php the_title(); ?></h2>
						<?php if ( get_the_excerpt() ) : ?>
							<p class="cm-issue-card-excerpt"><?php echo esc_html( wp_trim_words( get_the_excerpt(), 20 ) ); ?></p>
						<?php endif; ?>
					</a>
					<?php
				endwhile;
				?>
			</div>
			<?php the_posts_pagination(); ?>
		<?php else : ?>
			<p><?php esc_html_e( 'No magazine issues have been published yet.', 'church-magazine' ); ?></p>
		<?php endif; ?>
	</main>
</div>

<?php
get_footer();

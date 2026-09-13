<?php
if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Shared rendering logic used by both the single-issue template and the
 * [church_magazine] shortcode, so there is exactly one markup source.
 */
class CM_Render {

	/**
	 * Articles belonging to an issue, in table-of-contents order.
	 *
	 * @return WP_Post[]
	 */
	public static function get_issue_articles( $issue_id ) {
		$query = new WP_Query(
			array(
				'post_type'      => CM_Post_Types::ARTICLE_POST_TYPE,
				'post_status'    => 'publish',
				'posts_per_page' => -1,
				'orderby'        => 'menu_order title',
				'order'          => 'ASC',
				'meta_key'       => '_cm_issue_id',
				'meta_value'     => (int) $issue_id,
				'no_found_rows'  => true,
			)
		);
		return $query->posts;
	}

	public static function render_issue( $issue_id ) {
		$issue = get_post( $issue_id );
		if ( ! $issue || CM_Post_Types::ISSUE_POST_TYPE !== $issue->post_type ) {
			echo '<p>' . esc_html__( 'Magazine issue not found.', 'church-magazine' ) . '</p>';
			return;
		}

		$articles      = self::get_issue_articles( $issue_id );
		$show_excerpts = '1' === CM_Settings::get_option( 'show_excerpts' );
		$pdf_id        = get_post_meta( $issue_id, '_cm_pdf_id', true );
		$pdf_url       = $pdf_id ? wp_get_attachment_url( $pdf_id ) : '';

		?>
		<div class="cm-magazine" id="cm-magazine-<?php echo esc_attr( $issue_id ); ?>">

			<header class="cm-issue-header">
				<?php if ( has_post_thumbnail( $issue_id ) ) : ?>
					<div class="cm-issue-cover"><?php echo get_the_post_thumbnail( $issue_id, 'medium' ); ?></div>
				<?php endif; ?>
				<div class="cm-issue-heading">
					<h1 class="cm-issue-title"><?php echo esc_html( get_the_title( $issue_id ) ); ?></h1>
					<?php if ( get_the_excerpt( $issue_id ) ) : ?>
						<p class="cm-issue-description"><?php echo esc_html( get_the_excerpt( $issue_id ) ); ?></p>
					<?php endif; ?>
					<?php if ( $pdf_url ) : ?>
						<p class="cm-issue-pdf">
							<a class="cm-pdf-link" href="<?php echo esc_url( $pdf_url ); ?>" target="_blank" rel="noopener noreferrer">
								<?php esc_html_e( 'Download PDF', 'church-magazine' ); ?>
							</a>
						</p>
					<?php endif; ?>
				</div>
			</header>

			<?php if ( empty( $articles ) ) : ?>

				<p class="cm-empty"><?php esc_html_e( 'This issue does not have any articles yet.', 'church-magazine' ); ?></p>

			<?php else : ?>

				<div class="cm-layout">
					<button type="button" class="cm-toc-toggle" aria-expanded="false">
						<?php esc_html_e( 'Contents', 'church-magazine' ); ?>
					</button>

					<nav class="cm-sidebar" aria-label="<?php esc_attr_e( 'Table of contents', 'church-magazine' ); ?>">
						<h2 class="cm-sidebar-title"><?php esc_html_e( 'Contents', 'church-magazine' ); ?></h2>
						<ol class="cm-toc">
							<?php foreach ( $articles as $index => $article ) : ?>
								<li class="cm-toc-item">
									<a href="#cm-article-<?php echo esc_attr( $article->ID ); ?>" data-target="cm-article-<?php echo esc_attr( $article->ID ); ?>">
										<span class="cm-toc-number"><?php echo esc_html( $index + 1 ); ?></span>
										<span class="cm-toc-text">
											<span class="cm-toc-item-title"><?php echo esc_html( get_the_title( $article ) ); ?></span>
											<?php if ( $show_excerpts && get_the_excerpt( $article ) ) : ?>
												<span class="cm-toc-item-excerpt"><?php echo esc_html( wp_trim_words( get_the_excerpt( $article ), 12 ) ); ?></span>
											<?php endif; ?>
										</span>
									</a>
								</li>
							<?php endforeach; ?>
						</ol>
					</nav>

					<div class="cm-content">
						<?php foreach ( $articles as $article ) : ?>
							<section id="cm-article-<?php echo esc_attr( $article->ID ); ?>" class="cm-article">
								<h2 class="cm-article-title"><?php echo esc_html( get_the_title( $article ) ); ?></h2>
								<?php $byline = get_post_meta( $article->ID, '_cm_byline', true ); ?>
								<?php if ( $byline ) : ?>
									<p class="cm-article-byline"><?php echo esc_html( $byline ); ?></p>
								<?php endif; ?>
								<?php if ( has_post_thumbnail( $article ) ) : ?>
									<div class="cm-article-image"><?php echo get_the_post_thumbnail( $article, 'large' ); ?></div>
								<?php endif; ?>
								<div class="cm-article-body">
									<?php echo apply_filters( 'the_content', $article->post_content ); ?>
								</div>
							</section>
						<?php endforeach; ?>
					</div>
				</div>

			<?php endif; ?>

		</div>
		<?php
	}
}

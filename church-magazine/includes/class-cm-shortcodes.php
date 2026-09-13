<?php
if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * [church_magazine] embeds a full issue (TOC + reading view) into any page.
 * [church_magazine_latest] shows a small teaser card for the newest issue.
 */
class CM_Shortcodes {

	public function __construct() {
		add_shortcode( 'church_magazine', array( $this, 'render_magazine' ) );
		add_shortcode( 'church_magazine_latest', array( $this, 'render_latest_teaser' ) );
	}

	public function render_magazine( $atts ) {
		$atts = shortcode_atts( array( 'id' => 0 ), $atts, 'church_magazine' );

		$issue_id = absint( $atts['id'] );
		if ( ! $issue_id ) {
			$issue_id = $this->get_latest_issue_id();
		}

		if ( ! $issue_id ) {
			return '<p>' . esc_html__( 'No magazine issues have been published yet.', 'church-magazine' ) . '</p>';
		}

		CM_Frontend::enqueue_assets();

		ob_start();
		CM_Render::render_issue( $issue_id );
		return ob_get_clean();
	}

	public function render_latest_teaser( $atts ) {
		$issue_id = $this->get_latest_issue_id();
		if ( ! $issue_id ) {
			return '';
		}

		ob_start();
		?>
		<div class="cm-latest-teaser">
			<?php if ( has_post_thumbnail( $issue_id ) ) : ?>
				<a href="<?php echo esc_url( get_permalink( $issue_id ) ); ?>" class="cm-latest-teaser-cover">
					<?php echo get_the_post_thumbnail( $issue_id, 'medium' ); ?>
				</a>
			<?php endif; ?>
			<div class="cm-latest-teaser-body">
				<span class="cm-latest-teaser-label"><?php esc_html_e( 'Latest Issue', 'church-magazine' ); ?></span>
				<h3><a href="<?php echo esc_url( get_permalink( $issue_id ) ); ?>"><?php echo esc_html( get_the_title( $issue_id ) ); ?></a></h3>
				<?php if ( get_the_excerpt( $issue_id ) ) : ?>
					<p><?php echo esc_html( get_the_excerpt( $issue_id ) ); ?></p>
				<?php endif; ?>
				<a class="cm-latest-teaser-button" href="<?php echo esc_url( get_permalink( $issue_id ) ); ?>"><?php esc_html_e( 'Read Now', 'church-magazine' ); ?></a>
			</div>
		</div>
		<?php
		return ob_get_clean();
	}

	private function get_latest_issue_id() {
		$latest = get_posts(
			array(
				'post_type'      => CM_Post_Types::ISSUE_POST_TYPE,
				'post_status'    => 'publish',
				'posts_per_page' => 1,
				'orderby'        => 'date',
				'order'          => 'DESC',
			)
		);
		return $latest ? $latest[0]->ID : 0;
	}
}

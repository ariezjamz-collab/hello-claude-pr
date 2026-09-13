<?php
if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Groups everything under one "Church Magazine" admin menu and adds
 * helpful columns to the Issues/Articles list tables.
 */
class CM_Admin {

	public function __construct() {
		add_action( 'admin_menu', array( $this, 'register_menu' ) );
		add_action( 'admin_enqueue_scripts', array( $this, 'enqueue_admin_styles' ) );

		add_filter( 'manage_' . CM_Post_Types::ARTICLE_POST_TYPE . '_posts_columns', array( $this, 'article_columns' ) );
		add_action( 'manage_' . CM_Post_Types::ARTICLE_POST_TYPE . '_posts_custom_column', array( $this, 'render_article_column' ), 10, 2 );

		add_filter( 'manage_' . CM_Post_Types::ISSUE_POST_TYPE . '_posts_columns', array( $this, 'issue_columns' ) );
		add_action( 'manage_' . CM_Post_Types::ISSUE_POST_TYPE . '_posts_custom_column', array( $this, 'render_issue_column' ), 10, 2 );

		add_action( 'pre_get_posts', array( $this, 'filter_articles_by_issue' ) );
	}

	/**
	 * Supports the "Articles" count link on the Issues list table, which
	 * jumps to that issue's articles.
	 */
	public function filter_articles_by_issue( $query ) {
		if ( ! is_admin() || ! $query->is_main_query() ) {
			return;
		}
		if ( CM_Post_Types::ARTICLE_POST_TYPE !== $query->get( 'post_type' ) ) {
			return;
		}
		if ( empty( $_GET['cm_issue_filter'] ) ) {
			return;
		}
		$query->set(
			'meta_query',
			array(
				array(
					'key'   => '_cm_issue_id',
					'value' => absint( $_GET['cm_issue_filter'] ),
				),
			)
		);
	}

	public function register_menu() {
		add_menu_page(
			__( 'Church Magazine', 'church-magazine' ),
			__( 'Church Magazine', 'church-magazine' ),
			'edit_posts',
			'church-magazine',
			array( $this, 'render_dashboard' ),
			'dashicons-book-alt',
			25
		);

		// The dashboard page above also becomes the top item; rename the
		// duplicate first submenu WordPress adds automatically.
		add_submenu_page(
			'church-magazine',
			__( 'Overview', 'church-magazine' ),
			__( 'Overview', 'church-magazine' ),
			'edit_posts',
			'church-magazine',
			array( $this, 'render_dashboard' )
		);
	}

	public function enqueue_admin_styles( $hook ) {
		$screen = get_current_screen();
		if ( $screen && ( false !== strpos( $screen->id, 'church-magazine' ) || in_array( $screen->post_type, array( CM_Post_Types::ISSUE_POST_TYPE, CM_Post_Types::ARTICLE_POST_TYPE ), true ) ) ) {
			wp_enqueue_style( 'cm-admin', CM_PLUGIN_URL . 'assets/css/admin.css', array(), CM_VERSION );
		}
	}

	public function render_dashboard() {
		$issue_count   = wp_count_posts( CM_Post_Types::ISSUE_POST_TYPE )->publish;
		$article_count = wp_count_posts( CM_Post_Types::ARTICLE_POST_TYPE )->publish;
		?>
		<div class="wrap cm-dashboard">
			<h1><?php esc_html_e( 'Church Magazine', 'church-magazine' ); ?></h1>
			<p class="cm-intro"><?php esc_html_e( 'Publish digital magazine issues with a table of contents on the left and the reading content in the center.', 'church-magazine' ); ?></p>

			<div class="cm-stats">
				<div class="cm-stat-box">
					<span class="cm-stat-number"><?php echo esc_html( $issue_count ); ?></span>
					<span class="cm-stat-label"><?php esc_html_e( 'Published Issues', 'church-magazine' ); ?></span>
				</div>
				<div class="cm-stat-box">
					<span class="cm-stat-number"><?php echo esc_html( $article_count ); ?></span>
					<span class="cm-stat-label"><?php esc_html_e( 'Published Articles', 'church-magazine' ); ?></span>
				</div>
			</div>

			<div class="cm-quick-actions">
				<a class="button button-primary button-hero" href="<?php echo esc_url( admin_url( 'post-new.php?post_type=' . CM_Post_Types::ISSUE_POST_TYPE ) ); ?>">
					<?php esc_html_e( '+ New Issue', 'church-magazine' ); ?>
				</a>
				<a class="button button-secondary button-hero" href="<?php echo esc_url( admin_url( 'post-new.php?post_type=' . CM_Post_Types::ARTICLE_POST_TYPE ) ); ?>">
					<?php esc_html_e( '+ New Article', 'church-magazine' ); ?>
				</a>
			</div>

			<div class="cm-guide">
				<h2><?php esc_html_e( 'Two ways to publish an issue', 'church-magazine' ); ?></h2>
				<p>
					<strong><?php esc_html_e( 'Classic mode', 'church-magazine' ); ?></strong>
					&mdash;
					<?php esc_html_e( 'best for short issues written directly in WordPress.', 'church-magazine' ); ?>
				</p>
				<ol>
					<li><?php esc_html_e( 'Create an Issue (e.g. "Fall 2026") and set its cover image and description.', 'church-magazine' ); ?></li>
					<li><?php esc_html_e( 'Create Articles for that issue (e.g. "Pastor\'s Note", "Youth Ministry Update"). On each article, choose which Issue it belongs to and set an Order number.', 'church-magazine' ); ?></li>
					<li><?php esc_html_e( 'Visitors open the Issue and see the article titles listed as a table of contents on the left; clicking one jumps straight to that article in the center.', 'church-magazine' ); ?></li>
				</ol>
				<p>
					<strong><?php esc_html_e( 'PDF Flipbook mode (recommended for long issues)', 'church-magazine' ); ?></strong>
					&mdash;
					<?php esc_html_e( 'best when your magazine is already a print-ready PDF, even a 400-page one.', 'church-magazine' ); ?>
				</p>
				<ol>
					<li><?php esc_html_e( 'Create an Issue, open its "Reading Mode & Magazine File" box, choose "PDF Flipbook", and upload the whole magazine PDF once.', 'church-magazine' ); ?></li>
					<li><?php esc_html_e( 'In the "Table of Contents" box, type each section title with the page number it starts on (usually 20–50 rows, even for a long issue) — no need to re-create every page as a post.', 'church-magazine' ); ?></li>
					<li><?php esc_html_e( 'Visitors get the same left menu / center reading layout, except the center is the actual PDF page; clicking a menu item jumps the viewer straight to that page.', 'church-magazine' ); ?></li>
				</ol>
				<p><?php esc_html_e( 'Fine-tune sidebar colors and width for either mode under Settings.', 'church-magazine' ); ?></p>
			</div>

			<div class="cm-links">
				<a href="<?php echo esc_url( admin_url( 'edit.php?post_type=' . CM_Post_Types::ISSUE_POST_TYPE ) ); ?>"><?php esc_html_e( 'Manage Issues', 'church-magazine' ); ?></a> |
				<a href="<?php echo esc_url( admin_url( 'edit.php?post_type=' . CM_Post_Types::ARTICLE_POST_TYPE ) ); ?>"><?php esc_html_e( 'Manage Articles', 'church-magazine' ); ?></a> |
				<a href="<?php echo esc_url( admin_url( 'admin.php?page=church-magazine-settings' ) ); ?>"><?php esc_html_e( 'Settings', 'church-magazine' ); ?></a>
			</div>
		</div>
		<?php
	}

	public function article_columns( $columns ) {
		$new = array();
		foreach ( $columns as $key => $label ) {
			$new[ $key ] = $label;
			if ( 'title' === $key ) {
				$new['cm_issue'] = __( 'Issue', 'church-magazine' );
				$new['cm_order'] = __( 'Order', 'church-magazine' );
			}
		}
		return $new;
	}

	public function render_article_column( $column, $post_id ) {
		if ( 'cm_issue' === $column ) {
			$issue_id = (int) get_post_meta( $post_id, '_cm_issue_id', true );
			if ( $issue_id && get_post( $issue_id ) ) {
				echo '<a href="' . esc_url( get_edit_post_link( $issue_id ) ) . '">' . esc_html( get_the_title( $issue_id ) ) . '</a>';
			} else {
				echo '<span style="color:#b32d2e;">' . esc_html__( 'No issue assigned', 'church-magazine' ) . '</span>';
			}
		}

		if ( 'cm_order' === $column ) {
			echo esc_html( get_post_field( 'menu_order', $post_id ) );
		}
	}

	public function issue_columns( $columns ) {
		$new = array();
		foreach ( $columns as $key => $label ) {
			$new[ $key ] = $label;
			if ( 'title' === $key ) {
				$new['cm_articles'] = __( 'Articles', 'church-magazine' );
			}
		}
		return $new;
	}

	public function render_issue_column( $column, $post_id ) {
		if ( 'cm_articles' === $column ) {
			$count = count( CM_Render::get_issue_articles( $post_id ) );
			$url   = admin_url( 'edit.php?post_type=' . CM_Post_Types::ARTICLE_POST_TYPE . '&cm_issue_filter=' . $post_id );
			echo '<a href="' . esc_url( $url ) . '">' . esc_html( $count ) . '</a>';
		}
	}
}

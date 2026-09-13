<?php
if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Loads the reading-view template for issues and enqueues front-end assets.
 */
class CM_Frontend {

	public function __construct() {
		add_filter( 'template_include', array( $this, 'load_templates' ) );
		add_action( 'wp_enqueue_scripts', array( $this, 'maybe_enqueue_assets' ) );
	}

	public function load_templates( $template ) {
		if ( is_singular( CM_Post_Types::ISSUE_POST_TYPE ) ) {
			$theme_template = locate_template( array( 'single-magazine_issue.php' ) );
			return $theme_template ? $theme_template : CM_PLUGIN_DIR . 'templates/single-magazine_issue.php';
		}

		if ( is_post_type_archive( CM_Post_Types::ISSUE_POST_TYPE ) ) {
			$theme_template = locate_template( array( 'archive-magazine_issue.php' ) );
			return $theme_template ? $theme_template : CM_PLUGIN_DIR . 'templates/archive-magazine_issue.php';
		}

		return $template;
	}

	public function maybe_enqueue_assets() {
		if ( is_singular( CM_Post_Types::ISSUE_POST_TYPE ) ) {
			self::enqueue_assets();
			if ( 'pdf' === CM_Render::get_issue_mode( get_the_ID() ) ) {
				self::enqueue_pdf_assets();
			}
			return;
		}

		if ( is_post_type_archive( CM_Post_Types::ISSUE_POST_TYPE ) ) {
			self::enqueue_assets();
			return;
		}

		if ( is_a( get_post(), 'WP_Post' ) && has_shortcode( get_post()->post_content, 'church_magazine' ) ) {
			self::enqueue_assets();
		}
	}

	public static function enqueue_assets() {
		wp_enqueue_style( 'cm-frontend', CM_PLUGIN_URL . 'assets/css/frontend.css', array(), CM_VERSION );
		wp_enqueue_script( 'cm-frontend', CM_PLUGIN_URL . 'assets/js/frontend.js', array(), CM_VERSION, true );

		$sidebar_bg    = CM_Settings::get_option( 'sidebar_bg' );
		$sidebar_text  = CM_Settings::get_option( 'sidebar_text' );
		$accent_color  = CM_Settings::get_option( 'accent_color' );
		$sidebar_width = CM_Settings::get_option( 'sidebar_width' );
		$smooth_scroll = '1' === CM_Settings::get_option( 'smooth_scroll' );

		$css = sprintf(
			':root{--cm-sidebar-bg:%1$s;--cm-sidebar-text:%2$s;--cm-accent:%3$s;--cm-sidebar-width:%4$dpx;}',
			esc_html( $sidebar_bg ),
			esc_html( $sidebar_text ),
			esc_html( $accent_color ),
			absint( $sidebar_width )
		);
		wp_add_inline_style( 'cm-frontend', $css );

		wp_localize_script(
			'cm-frontend',
			'ChurchMagazine',
			array( 'smoothScroll' => $smooth_scroll )
		);
	}

	public static function enqueue_pdf_assets() {
		wp_enqueue_script( 'pdfjs', CM_PLUGIN_URL . 'assets/vendor/pdfjs/pdf.min.js', array(), CM_VERSION, true );
		wp_enqueue_script( 'cm-pdf-viewer', CM_PLUGIN_URL . 'assets/js/pdf-viewer.js', array( 'pdfjs' ), CM_VERSION, true );

		wp_localize_script(
			'cm-pdf-viewer',
			'ChurchMagazinePdf',
			array( 'workerSrc' => CM_PLUGIN_URL . 'assets/vendor/pdfjs/pdf.worker.min.js' )
		);
	}
}

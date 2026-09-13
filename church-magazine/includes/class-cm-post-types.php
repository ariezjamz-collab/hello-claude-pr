<?php
if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Registers the "Magazine Issue" and "Magazine Article" post types.
 */
class CM_Post_Types {

	const ISSUE_POST_TYPE   = 'magazine_issue';
	const ARTICLE_POST_TYPE = 'magazine_article';

	public function __construct() {
		add_action( 'init', array( $this, 'register_issue_post_type' ) );
		add_action( 'init', array( $this, 'register_article_post_type' ) );
		add_action( 'template_redirect', array( $this, 'redirect_single_article_to_issue' ) );
	}

	public function register_issue_post_type() {
		$labels = array(
			'name'               => __( 'Magazine Issues', 'church-magazine' ),
			'singular_name'      => __( 'Magazine Issue', 'church-magazine' ),
			'add_new_item'       => __( 'Add New Issue', 'church-magazine' ),
			'edit_item'          => __( 'Edit Issue', 'church-magazine' ),
			'new_item'           => __( 'New Issue', 'church-magazine' ),
			'view_item'          => __( 'View Issue', 'church-magazine' ),
			'search_items'       => __( 'Search Issues', 'church-magazine' ),
			'not_found'          => __( 'No issues found', 'church-magazine' ),
			'all_items'          => __( 'All Issues', 'church-magazine' ),
			'menu_name'          => __( 'Issues', 'church-magazine' ),
			'featured_image'     => __( 'Cover Image', 'church-magazine' ),
			'set_featured_image' => __( 'Set cover image', 'church-magazine' ),
		);

		register_post_type(
			self::ISSUE_POST_TYPE,
			array(
				'labels'        => $labels,
				'public'        => true,
				'has_archive'   => 'magazine',
				'rewrite'       => array( 'slug' => 'magazine', 'with_front' => false ),
				'supports'      => array( 'title', 'editor', 'excerpt', 'thumbnail' ),
				'show_in_menu'  => 'church-magazine',
				'menu_icon'     => 'dashicons-book-alt',
				'show_in_rest'  => true,
				'capability_type' => 'post',
			)
		);
	}

	public function register_article_post_type() {
		$labels = array(
			'name'               => __( 'Magazine Articles', 'church-magazine' ),
			'singular_name'      => __( 'Magazine Article', 'church-magazine' ),
			'add_new_item'       => __( 'Add New Article', 'church-magazine' ),
			'edit_item'          => __( 'Edit Article', 'church-magazine' ),
			'new_item'           => __( 'New Article', 'church-magazine' ),
			'view_item'          => __( 'View Article', 'church-magazine' ),
			'search_items'       => __( 'Search Articles', 'church-magazine' ),
			'not_found'          => __( 'No articles found', 'church-magazine' ),
			'all_items'          => __( 'All Articles', 'church-magazine' ),
			'menu_name'          => __( 'Articles', 'church-magazine' ),
			'featured_image'     => __( 'Article Image', 'church-magazine' ),
			'set_featured_image' => __( 'Set article image', 'church-magazine' ),
		);

		register_post_type(
			self::ARTICLE_POST_TYPE,
			array(
				'labels'        => $labels,
				'public'        => true,
				'has_archive'   => false,
				'rewrite'       => false,
				'supports'      => array( 'title', 'editor', 'excerpt', 'thumbnail', 'page-attributes' ),
				'show_in_menu'  => 'church-magazine',
				'show_in_rest'  => true,
				'capability_type' => 'post',
			)
		);
	}

	/**
	 * Articles are meant to be read inside their issue, not standalone.
	 * Visiting an article's own URL sends the reader straight to its
	 * place in the issue instead of showing a bare, out-of-context page.
	 */
	public function redirect_single_article_to_issue() {
		if ( ! is_singular( self::ARTICLE_POST_TYPE ) ) {
			return;
		}

		$article_id = get_queried_object_id();
		$issue_id   = (int) get_post_meta( $article_id, '_cm_issue_id', true );

		if ( $issue_id && get_post( $issue_id ) ) {
			$url = get_permalink( $issue_id ) . '#cm-article-' . $article_id;
			wp_safe_redirect( $url, 301 );
			exit;
		}
	}
}

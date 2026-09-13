<?php
if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Meta boxes that link an article to its issue, and let an issue carry
 * a downloadable PDF.
 */
class CM_Metaboxes {

	public function __construct() {
		add_action( 'add_meta_boxes', array( $this, 'add_article_metabox' ) );
		add_action( 'add_meta_boxes', array( $this, 'add_issue_metabox' ) );
		add_action( 'save_post_' . CM_Post_Types::ARTICLE_POST_TYPE, array( $this, 'save_article_meta' ) );
		add_action( 'save_post_' . CM_Post_Types::ISSUE_POST_TYPE, array( $this, 'save_issue_meta' ) );
		add_action( 'admin_enqueue_scripts', array( $this, 'enqueue_media_uploader' ) );
	}

	public function add_article_metabox() {
		add_meta_box(
			'cm_article_details',
			__( 'Magazine Details', 'church-magazine' ),
			array( $this, 'render_article_metabox' ),
			CM_Post_Types::ARTICLE_POST_TYPE,
			'side',
			'high'
		);
	}

	public function add_issue_metabox() {
		add_meta_box(
			'cm_issue_pdf',
			__( 'Downloadable PDF (optional)', 'church-magazine' ),
			array( $this, 'render_issue_metabox' ),
			CM_Post_Types::ISSUE_POST_TYPE,
			'side',
			'default'
		);
	}

	public function render_article_metabox( $post ) {
		wp_nonce_field( 'cm_save_article_meta', 'cm_article_nonce' );

		$selected_issue = get_post_meta( $post->ID, '_cm_issue_id', true );
		$byline         = get_post_meta( $post->ID, '_cm_byline', true );

		$issues = get_posts(
			array(
				'post_type'      => CM_Post_Types::ISSUE_POST_TYPE,
				'posts_per_page' => -1,
				'orderby'        => 'date',
				'order'          => 'DESC',
				'post_status'    => array( 'publish', 'draft', 'pending', 'future' ),
			)
		);
		?>
		<p>
			<label for="cm_issue_id"><strong><?php esc_html_e( 'Belongs to Issue', 'church-magazine' ); ?></strong></label><br />
			<select name="cm_issue_id" id="cm_issue_id" style="width:100%;">
				<option value=""><?php esc_html_e( '— Select an issue —', 'church-magazine' ); ?></option>
				<?php foreach ( $issues as $issue ) : ?>
					<option value="<?php echo esc_attr( $issue->ID ); ?>" <?php selected( $selected_issue, $issue->ID ); ?>>
						<?php echo esc_html( $issue->post_title ); ?>
					</option>
				<?php endforeach; ?>
			</select>
		</p>
		<p>
			<label for="cm_byline"><strong><?php esc_html_e( 'Byline / Author', 'church-magazine' ); ?></strong></label><br />
			<input type="text" name="cm_byline" id="cm_byline" style="width:100%;" value="<?php echo esc_attr( $byline ); ?>" placeholder="<?php esc_attr_e( 'e.g. Pastor John Smith', 'church-magazine' ); ?>" />
		</p>
		<p class="description">
			<?php esc_html_e( 'Use the "Order" field below to control where this article appears in the table of contents.', 'church-magazine' ); ?>
		</p>
		<?php
	}

	public function save_article_meta( $post_id ) {
		if ( ! isset( $_POST['cm_article_nonce'] ) || ! wp_verify_nonce( $_POST['cm_article_nonce'], 'cm_save_article_meta' ) ) {
			return;
		}
		if ( defined( 'DOING_AUTOSAVE' ) && DOING_AUTOSAVE ) {
			return;
		}
		if ( ! current_user_can( 'edit_post', $post_id ) ) {
			return;
		}

		if ( isset( $_POST['cm_issue_id'] ) ) {
			update_post_meta( $post_id, '_cm_issue_id', absint( $_POST['cm_issue_id'] ) );
		}
		if ( isset( $_POST['cm_byline'] ) ) {
			update_post_meta( $post_id, '_cm_byline', sanitize_text_field( wp_unslash( $_POST['cm_byline'] ) ) );
		}
	}

	public function render_issue_metabox( $post ) {
		wp_nonce_field( 'cm_save_issue_meta', 'cm_issue_nonce' );
		$pdf_id  = get_post_meta( $post->ID, '_cm_pdf_id', true );
		$pdf_url = $pdf_id ? wp_get_attachment_url( $pdf_id ) : '';
		?>
		<p>
			<?php esc_html_e( 'Attach a print-ready PDF so visitors can download the full issue.', 'church-magazine' ); ?>
		</p>
		<input type="hidden" name="cm_pdf_id" id="cm_pdf_id" value="<?php echo esc_attr( $pdf_id ); ?>" />
		<p>
			<button type="button" class="button" id="cm_pdf_upload_button"><?php esc_html_e( 'Select PDF', 'church-magazine' ); ?></button>
			<button type="button" class="button" id="cm_pdf_remove_button" <?php echo $pdf_id ? '' : 'style="display:none;"'; ?>><?php esc_html_e( 'Remove', 'church-magazine' ); ?></button>
		</p>
		<p id="cm_pdf_filename">
			<?php if ( $pdf_url ) : ?>
				<a href="<?php echo esc_url( $pdf_url ); ?>" target="_blank" rel="noopener noreferrer"><?php echo esc_html( basename( $pdf_url ) ); ?></a>
			<?php endif; ?>
		</p>
		<?php
	}

	public function save_issue_meta( $post_id ) {
		if ( ! isset( $_POST['cm_issue_nonce'] ) || ! wp_verify_nonce( $_POST['cm_issue_nonce'], 'cm_save_issue_meta' ) ) {
			return;
		}
		if ( defined( 'DOING_AUTOSAVE' ) && DOING_AUTOSAVE ) {
			return;
		}
		if ( ! current_user_can( 'edit_post', $post_id ) ) {
			return;
		}

		if ( isset( $_POST['cm_pdf_id'] ) ) {
			update_post_meta( $post_id, '_cm_pdf_id', absint( $_POST['cm_pdf_id'] ) );
		}
	}

	public function enqueue_media_uploader( $hook ) {
		global $post_type;
		if ( in_array( $hook, array( 'post.php', 'post-new.php' ), true ) && CM_Post_Types::ISSUE_POST_TYPE === $post_type ) {
			wp_enqueue_media();
			wp_enqueue_script(
				'cm-admin-pdf-uploader',
				CM_PLUGIN_URL . 'assets/js/admin-pdf-uploader.js',
				array( 'jquery' ),
				CM_VERSION,
				true
			);
		}
	}
}

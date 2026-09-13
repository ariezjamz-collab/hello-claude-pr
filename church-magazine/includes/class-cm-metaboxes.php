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
			__( 'Reading Mode & Magazine File', 'church-magazine' ),
			array( $this, 'render_issue_metabox' ),
			CM_Post_Types::ISSUE_POST_TYPE,
			'side',
			'default'
		);

		add_meta_box(
			'cm_issue_toc',
			__( 'Table of Contents (for PDF Flipbook)', 'church-magazine' ),
			array( $this, 'render_toc_metabox' ),
			CM_Post_Types::ISSUE_POST_TYPE,
			'normal',
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
		$mode    = get_post_meta( $post->ID, '_cm_issue_mode', true );
		$mode    = $mode ? $mode : 'classic';
		?>
		<p><strong><?php esc_html_e( 'Reading Mode', 'church-magazine' ); ?></strong></p>
		<p>
			<label>
				<input type="radio" name="cm_issue_mode" value="classic" <?php checked( $mode, 'classic' ); ?> />
				<?php esc_html_e( 'Classic — articles typed into WordPress', 'church-magazine' ); ?>
			</label><br />
			<label>
				<input type="radio" name="cm_issue_mode" value="pdf" <?php checked( $mode, 'pdf' ); ?> />
				<?php esc_html_e( 'PDF Flipbook — upload one print-ready PDF', 'church-magazine' ); ?>
			</label>
		</p>
		<p class="description">
			<?php esc_html_e( 'PDF Flipbook is the fastest way to publish a long issue: upload the whole magazine once and type a short table of contents below instead of re-creating every page as an article.', 'church-magazine' ); ?>
		</p>
		<hr />
		<p>
			<strong><?php esc_html_e( 'Magazine PDF', 'church-magazine' ); ?></strong><br />
			<?php esc_html_e( 'Used as the downloadable file, and as the on-screen viewer when Reading Mode is PDF Flipbook.', 'church-magazine' ); ?>
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

	public function render_toc_metabox( $post ) {
		$rows = get_post_meta( $post->ID, '_cm_toc', true );
		if ( ! is_array( $rows ) || empty( $rows ) ) {
			$rows = array( array( 'title' => '', 'page' => '' ) );
		}
		?>
		<p class="description">
			<?php esc_html_e( 'Only used when Reading Mode is set to PDF Flipbook. List each section title with the page number it starts on (e.g. "Pastor\'s Note" starting on page 3). Visitors click these in the left-hand menu to jump straight to that page.', 'church-magazine' ); ?>
		</p>
		<table class="widefat cm-toc-table" id="cm_toc_table">
			<thead>
				<tr>
					<th><?php esc_html_e( 'Section Title', 'church-magazine' ); ?></th>
					<th style="width:120px;"><?php esc_html_e( 'Start Page', 'church-magazine' ); ?></th>
					<th style="width:110px;"></th>
				</tr>
			</thead>
			<tbody>
				<?php foreach ( $rows as $row ) : ?>
					<tr class="cm-toc-row">
						<td><input type="text" class="widefat" name="cm_toc_title[]" value="<?php echo esc_attr( $row['title'] ?? '' ); ?>" placeholder="<?php esc_attr_e( "e.g. Pastor's Note", 'church-magazine' ); ?>" /></td>
						<td><input type="number" min="1" class="small-text" name="cm_toc_page[]" value="<?php echo esc_attr( $row['page'] ?? '' ); ?>" /></td>
						<td class="cm-toc-row-actions">
							<button type="button" class="button cm-toc-move-up" aria-label="<?php esc_attr_e( 'Move up', 'church-magazine' ); ?>">&uarr;</button>
							<button type="button" class="button cm-toc-move-down" aria-label="<?php esc_attr_e( 'Move down', 'church-magazine' ); ?>">&darr;</button>
							<button type="button" class="button cm-toc-remove-row" aria-label="<?php esc_attr_e( 'Remove', 'church-magazine' ); ?>">&times;</button>
						</td>
					</tr>
				<?php endforeach; ?>
			</tbody>
		</table>
		<p>
			<button type="button" class="button button-secondary" id="cm_toc_add_row"><?php esc_html_e( '+ Add Section', 'church-magazine' ); ?></button>
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

		if ( isset( $_POST['cm_issue_mode'] ) && in_array( $_POST['cm_issue_mode'], array( 'classic', 'pdf' ), true ) ) {
			update_post_meta( $post_id, '_cm_issue_mode', $_POST['cm_issue_mode'] );
		}

		if ( isset( $_POST['cm_toc_title'] ) && is_array( $_POST['cm_toc_title'] ) ) {
			$titles = wp_unslash( $_POST['cm_toc_title'] );
			$pages  = isset( $_POST['cm_toc_page'] ) ? $_POST['cm_toc_page'] : array();
			$toc    = array();

			foreach ( $titles as $index => $title ) {
				$title = sanitize_text_field( $title );
				if ( '' === $title ) {
					continue;
				}
				$page = isset( $pages[ $index ] ) ? absint( $pages[ $index ] ) : 0;
				$toc[] = array(
					'title' => $title,
					'page'  => $page ? $page : 1,
				);
			}

			update_post_meta( $post_id, '_cm_toc', $toc );
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
			wp_enqueue_script(
				'cm-admin-toc-repeater',
				CM_PLUGIN_URL . 'assets/js/admin-toc-repeater.js',
				array( 'jquery' ),
				CM_VERSION,
				true
			);
			wp_enqueue_style( 'cm-admin', CM_PLUGIN_URL . 'assets/css/admin.css', array(), CM_VERSION );
		}
	}
}

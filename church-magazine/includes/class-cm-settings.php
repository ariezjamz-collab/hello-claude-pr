<?php
if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * A single Settings page (Settings API) for the magazine's look and feel.
 */
class CM_Settings {

	const OPTION_KEY = 'cm_settings';

	public function __construct() {
		add_action( 'admin_menu', array( $this, 'register_settings_page' ) );
		add_action( 'admin_init', array( $this, 'register_settings' ) );
	}

	public static function get_defaults() {
		return array(
			'sidebar_bg'     => '#1b2a4a',
			'sidebar_text'   => '#ffffff',
			'accent_color'   => '#c9a227',
			'sidebar_width'  => '280',
			'show_excerpts'  => '1',
			'smooth_scroll'  => '1',
		);
	}

	public static function get_option( $key ) {
		$options  = get_option( self::OPTION_KEY, array() );
		$defaults = self::get_defaults();
		return isset( $options[ $key ] ) ? $options[ $key ] : $defaults[ $key ];
	}

	public function register_settings_page() {
		add_submenu_page(
			'church-magazine',
			__( 'Magazine Settings', 'church-magazine' ),
			__( 'Settings', 'church-magazine' ),
			'manage_options',
			'church-magazine-settings',
			array( $this, 'render_settings_page' )
		);
	}

	public function register_settings() {
		register_setting( 'cm_settings_group', self::OPTION_KEY, array( $this, 'sanitize' ) );

		add_settings_section( 'cm_appearance', __( 'Appearance', 'church-magazine' ), '__return_false', 'church-magazine-settings' );

		add_settings_field( 'sidebar_bg', __( 'Sidebar Background Color', 'church-magazine' ), array( $this, 'field_color' ), 'church-magazine-settings', 'cm_appearance', array( 'key' => 'sidebar_bg' ) );
		add_settings_field( 'sidebar_text', __( 'Sidebar Text Color', 'church-magazine' ), array( $this, 'field_color' ), 'church-magazine-settings', 'cm_appearance', array( 'key' => 'sidebar_text' ) );
		add_settings_field( 'accent_color', __( 'Accent Color (active item)', 'church-magazine' ), array( $this, 'field_color' ), 'church-magazine-settings', 'cm_appearance', array( 'key' => 'accent_color' ) );
		add_settings_field( 'sidebar_width', __( 'Sidebar Width (px)', 'church-magazine' ), array( $this, 'field_number' ), 'church-magazine-settings', 'cm_appearance', array( 'key' => 'sidebar_width' ) );
		add_settings_field( 'show_excerpts', __( 'Show article excerpts in menu', 'church-magazine' ), array( $this, 'field_checkbox' ), 'church-magazine-settings', 'cm_appearance', array( 'key' => 'show_excerpts' ) );
		add_settings_field( 'smooth_scroll', __( 'Enable smooth scrolling', 'church-magazine' ), array( $this, 'field_checkbox' ), 'church-magazine-settings', 'cm_appearance', array( 'key' => 'smooth_scroll' ) );
	}

	public function sanitize( $input ) {
		$output    = array();
		$defaults  = self::get_defaults();

		$output['sidebar_bg']    = $this->sanitize_hex_color( $input['sidebar_bg'] ?? '', $defaults['sidebar_bg'] );
		$output['sidebar_text']  = $this->sanitize_hex_color( $input['sidebar_text'] ?? '', $defaults['sidebar_text'] );
		$output['accent_color']  = $this->sanitize_hex_color( $input['accent_color'] ?? '', $defaults['accent_color'] );
		$output['sidebar_width'] = max( 180, min( 500, absint( $input['sidebar_width'] ?? $defaults['sidebar_width'] ) ) );
		$output['show_excerpts'] = ! empty( $input['show_excerpts'] ) ? '1' : '0';
		$output['smooth_scroll'] = ! empty( $input['smooth_scroll'] ) ? '1' : '0';

		return $output;
	}

	private function sanitize_hex_color( $value, $fallback ) {
		$value = sanitize_hex_color( $value );
		return $value ? $value : $fallback;
	}

	public function field_color( $args ) {
		$key   = $args['key'];
		$value = self::get_option( $key );
		printf(
			'<input type="text" class="cm-color-field" name="%1$s[%2$s]" value="%3$s" data-default-color="%4$s" />',
			esc_attr( self::OPTION_KEY ),
			esc_attr( $key ),
			esc_attr( $value ),
			esc_attr( self::get_defaults()[ $key ] )
		);
	}

	public function field_number( $args ) {
		$key   = $args['key'];
		$value = self::get_option( $key );
		printf(
			'<input type="number" min="180" max="500" name="%1$s[%2$s]" value="%3$s" class="small-text" />',
			esc_attr( self::OPTION_KEY ),
			esc_attr( $key ),
			esc_attr( $value )
		);
	}

	public function field_checkbox( $args ) {
		$key   = $args['key'];
		$value = self::get_option( $key );
		printf(
			'<label><input type="checkbox" name="%1$s[%2$s]" value="1" %3$s /> %4$s</label>',
			esc_attr( self::OPTION_KEY ),
			esc_attr( $key ),
			checked( $value, '1', false ),
			esc_html__( 'Enabled', 'church-magazine' )
		);
	}

	public function render_settings_page() {
		wp_enqueue_style( 'wp-color-picker' );
		wp_enqueue_script( 'wp-color-picker' );
		wp_add_inline_script( 'wp-color-picker', 'jQuery(function($){$(".cm-color-field").wpColorPicker();});' );
		?>
		<div class="wrap">
			<h1><?php esc_html_e( 'Magazine Settings', 'church-magazine' ); ?></h1>
			<form method="post" action="options.php">
				<?php
				settings_fields( 'cm_settings_group' );
				do_settings_sections( 'church-magazine-settings' );
				submit_button();
				?>
			</form>
		</div>
		<?php
	}
}

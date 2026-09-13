<?php
/**
 * Plugin Name:       Church Magazine
 * Plugin URI:        https://github.com/ariezjamz-collab/hello-claude-pr
 * Description:       Publish digital magazine issues with a scrolling table of contents on the left and the magazine content in the center. Includes a simple backend for managing issues and articles.
 * Version:           1.0.0
 * Requires at least: 5.8
 * Requires PHP:      7.4
 * Author:            Church Magazine
 * License:           GPL v2 or later
 * License URI:       https://www.gnu.org/licenses/gpl-2.0.html
 * Text Domain:       church-magazine
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit; // No direct access.
}

define( 'CM_VERSION', '1.0.0' );
define( 'CM_PLUGIN_FILE', __FILE__ );
define( 'CM_PLUGIN_DIR', plugin_dir_path( __FILE__ ) );
define( 'CM_PLUGIN_URL', plugin_dir_url( __FILE__ ) );

require_once CM_PLUGIN_DIR . 'includes/class-cm-post-types.php';
require_once CM_PLUGIN_DIR . 'includes/class-cm-metaboxes.php';
require_once CM_PLUGIN_DIR . 'includes/class-cm-admin.php';
require_once CM_PLUGIN_DIR . 'includes/class-cm-settings.php';
require_once CM_PLUGIN_DIR . 'includes/class-cm-render.php';
require_once CM_PLUGIN_DIR . 'includes/class-cm-shortcodes.php';
require_once CM_PLUGIN_DIR . 'includes/class-cm-frontend.php';

/**
 * Boots the plugin and wires up all the pieces.
 */
final class Church_Magazine {

	private static $instance = null;

	public static function instance() {
		if ( null === self::$instance ) {
			self::$instance = new self();
		}
		return self::$instance;
	}

	private function __construct() {
		new CM_Post_Types();
		new CM_Metaboxes();
		new CM_Admin();
		new CM_Settings();
		new CM_Shortcodes();
		new CM_Frontend();
	}
}

/**
 * Registers the post types early (needed on activation before the rest of the plugin boots).
 */
function cm_register_post_types_for_activation() {
	$post_types = new CM_Post_Types();
	$post_types->register_issue_post_type();
	$post_types->register_article_post_type();
}

function cm_activate() {
	cm_register_post_types_for_activation();
	flush_rewrite_rules();
}
register_activation_hook( __FILE__, 'cm_activate' );

function cm_deactivate() {
	flush_rewrite_rules();
}
register_deactivation_hook( __FILE__, 'cm_deactivate' );

add_action( 'plugins_loaded', array( 'Church_Magazine', 'instance' ) );
